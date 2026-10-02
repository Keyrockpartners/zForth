package compiler

import (
	"fmt"
	"go/ast"
	"go/constant"
	"go/token"
	"go/types"
	"strings"
)

// endlocalsTok stands for endlocals until the frame size is known; it is
// dropped if the function needs no frame.
const endlocalsTok = "\x00endlocals"

// fgen generates one function. Locals live in a numeric frame on the
// return stack (ZGO_PLAN.md 5.2): parameters first, then named results,
// then locals and temporaries, two slots for two-cell values. Slots are
// reused once a block ends.
type fgen struct {
	c  *compiler
	g  *progGen
	fn *funcInfo // nil for z-init

	toks  []string
	depth int

	slots  map[*types.Var]int
	next   int // next free slot
	max    int // frame size
	params int // parameter cells

	sig     *types.Signature
	results []int // slots of the named results, if any

	labels [32]bool
	brk    []*branchCtx
}

type branchCtx struct {
	label string // Go label, "" if none
	loop  bool
	brk   int // label number for break
	cont  int // label number for continue, -1 for switch
}

func newFuncGen(c *compiler, g *progGen, fi *funcInfo) *fgen {
	return &fgen{c: c, g: g, fn: fi, slots: map[*types.Var]int{}}
}

func (f *fgen) emit(toks ...string) { f.toks = append(f.toks, toks...) }

func (f *fgen) nl() {
	f.toks = append(f.toks, "\n"+strings.Repeat("  ", f.depth+1))
}

func (f *fgen) errorf(pos token.Pos, format string, args ...any) {
	f.c.errorf(pos, format, args...)
}

// allocSlots reserves n frame slots.
func (f *fgen) allocSlots(n int) int {
	k := f.next
	f.next += n
	if f.next > f.max {
		f.max = f.next
	}
	return k
}

// declare gives a local variable its storage: frame slots for scalars,
// static memory for arrays and structs.
func (f *fgen) declare(v *types.Var) {
	if kindOf(v.Type()).inMemory() {
		if f.fn != nil && f.fn.recursive {
			f.errorf(v.Pos(), msgLocalArray)
		}
		owner := "init"
		if f.fn != nil {
			owner = strings.TrimPrefix(strings.TrimPrefix(f.fn.word, "z."), "z-")
		}
		name := f.c.unique(shorten("z." + owner + "." + v.Name()))
		f.c.statics[v] = f.g.alloc(name, sizeof(v.Type()), alignof(v.Type()), "local "+v.Name()+" "+typeName(v.Type()))
		return
	}
	f.slots[v] = f.allocSlots(cells(v.Type()))
}

// temp reserves anonymous slots for a value of type t.
func (f *fgen) temp(t types.Type) int { return f.allocSlots(cells(t)) }

// staticTemp reserves anonymous static memory, for composite literals.
func (f *fgen) staticTemp(t types.Type, pos token.Pos) string {
	if f.fn != nil && f.fn.recursive {
		f.errorf(pos, "composite literals, and arrays or structs swapped in a multiple assignment, need static storage, which recursive functions cannot use: declare the value at package level")
	}
	return f.g.alloc(f.c.unique("z:lit"), sizeof(t), alignof(t), "literal "+typeName(t))
}

func (f *fgen) loadSlot(k int, t types.Type) {
	if cells(t) == 2 {
		f.emit(itoa(k), "2l@")
	} else {
		f.emit(itoa(k), "l@")
	}
}

func (f *fgen) storeSlot(k int, t types.Type) {
	if cells(t) == 2 {
		f.emit(itoa(k), "2l!")
	} else {
		f.emit(itoa(k), "l!")
	}
}

func itoa(n int) string { return fmt.Sprint(n) }

// newLabel allocates a jump label number for the zgoto words in zgo.zf.
func (f *fgen) newLabel(pos token.Pos) int {
	for i := range f.labels {
		if !f.labels[i] {
			f.labels[i] = true
			f.emit("znew", itoa(i))
			return i
		}
	}
	f.errorf(pos, "statements nested too deeply (more than 32 open loops, switches and switch cases)")
	return 0
}

func (f *fgen) freeLabel(n int) {
	if n >= 0 {
		f.labels[n] = false
	}
}

// function generates a function definition.
func (f *fgen) function() string {
	fd := f.fn.decl
	f.sig = f.fn.obj.Type().(*types.Signature)
	if r := f.sig.Recv(); r != nil {
		f.param(r)
	}
	for i := 0; i < f.sig.Params().Len(); i++ {
		f.param(f.sig.Params().At(i))
	}
	f.params = f.next
	for i := 0; i < f.sig.Results().Len(); i++ {
		r := f.sig.Results().At(i)
		if r.Name() != "" {
			k := f.temp(r.Type())
			f.slots[r] = k
			f.results = append(f.results, k)
		}
	}
	// named results start at zero; frame zeroes slots past the parameters
	f.depth = 0
	f.block(fd.Body.List, true)
	return f.assemble(f.fn.word, f.header())
}

// param gives a parameter its slots; blank and unnamed parameters still
// take their slots.
func (f *fgen) param(v *types.Var) {
	if kindOf(v.Type()) == kStruct {
		// passed by address, read-only
		f.slots[v] = f.allocSlots(1)
		if pos, bad := f.c.modifies(f.fn.decl.Body, v); bad {
			f.errorf(pos, "struct parameters are read-only in ZGo (they are passed by address): pass a pointer, *%s", typeName(v.Type()))
		}
		return
	}
	f.slots[v] = f.allocSlots(cells(v.Type()))
}

func (f *fgen) header() string {
	fd := f.fn.decl
	var b strings.Builder
	b.WriteString("func ")
	if fd.Recv != nil {
		b.WriteString("(" + typeName(f.c.typeOf(fd.Recv.List[0].Type)) + ") ")
	}
	b.WriteString(fd.Name.Name)
	sig := strings.TrimPrefix(typeName(f.sig), "func")
	b.WriteString(sig)
	return b.String()
}

// assemble wraps the body in a definition with the frame prologue.
func (f *fgen) assemble(word, comment string) string {
	toks := f.toks
	var pro []string
	switch {
	case f.max == 0:
	case f.params == f.max:
		pro = []string{itoa(f.max), "locals"}
	default:
		pro = []string{itoa(f.params), itoa(f.max), "frame"}
	}
	body := make([]string, 0, len(toks)+4)
	body = append(body, pro...)
	for _, t := range toks {
		if t == endlocalsTok {
			if f.max == 0 {
				continue
			}
			t = "endlocals"
		}
		body = append(body, t)
	}
	if f.max > 0 {
		body = append(body, "endlocals")
	}
	if !f.c.opts.NoOptimize {
		body = peephole(body)
	}
	var b strings.Builder
	fmt.Fprintf(&b, "( %s )\n: %s", commentSafe(comment), word)
	col := 3 + len(word)
	for _, t := range body {
		if strings.HasPrefix(t, "\n") {
			b.WriteString(t)
			col = len(t) - 1
			continue
		}
		if col > 100 {
			b.WriteString("\n   ")
			col = 3
		}
		b.WriteByte(' ')
		b.WriteString(t)
		col += 1 + len(t)
	}
	b.WriteString(" ;\n")
	return trimBlankLines(b.String())
}

// trimBlankLines removes lines left empty by newline tokens.
func trimBlankLines(s string) string {
	lines := strings.Split(s, "\n")
	out := lines[:0]
	for _, l := range lines {
		if strings.TrimSpace(l) == "" {
			continue
		}
		out = append(out, strings.TrimRight(l, " "))
	}
	return strings.Join(out, "\n") + "\n"
}

// block generates a list of statements in a new scope. If last is set, a
// final return statement needs no exit.
func (f *fgen) block(list []ast.Stmt, last bool) {
	save := f.next
	for i, s := range list {
		f.stmt(s, last && i == len(list)-1)
	}
	f.next = save
}

func (f *fgen) stmt(s ast.Stmt, last bool) {
	f.nl()
	if f.c.opts.Comments {
		p := f.c.fset.Position(s.Pos())
		f.emit(fmt.Sprintf("( %s:%d )", baseName(p.Filename), p.Line))
	}
	switch s := s.(type) {
	case *ast.ExprStmt:
		f.expr(s.X)
		f.drop(f.c.typeOf(s.X), s.X)
	case *ast.AssignStmt:
		f.assignStmt(s)
	case *ast.IncDecStmt:
		one := &ast.BasicLit{Kind: token.INT, Value: "1"}
		op := token.ADD
		if s.Tok == token.DEC {
			op = token.SUB
		}
		f.opAssign(s.X, op, one, s.Pos())
	case *ast.DeclStmt:
		f.declStmt(s)
	case *ast.BlockStmt:
		f.block(s.List, last)
	case *ast.IfStmt:
		f.ifStmt(s, last)
	case *ast.ForStmt:
		f.forStmt(s, "")
	case *ast.RangeStmt:
		f.rangeStmt(s, "")
	case *ast.SwitchStmt:
		f.switchStmt(s, "", last)
	case *ast.LabeledStmt:
		switch t := s.Stmt.(type) {
		case *ast.ForStmt:
			f.forStmt(t, s.Label.Name)
		case *ast.RangeStmt:
			f.rangeStmt(t, s.Label.Name)
		case *ast.SwitchStmt:
			f.switchStmt(t, s.Label.Name, last)
		}
	case *ast.BranchStmt:
		f.branchStmt(s)
	case *ast.ReturnStmt:
		f.returnStmt(s, last)
	case *ast.EmptyStmt:
	default:
		f.errorf(s.Pos(), "unsupported statement")
	}
}

func baseName(p string) string {
	if i := strings.LastIndexAny(p, "/\\"); i >= 0 {
		return p[i+1:]
	}
	return p
}

// drop discards a value of type t (the results of a call statement).
func (f *fgen) drop(t types.Type, e ast.Expr) {
	if tup, ok := t.(*types.Tuple); ok {
		for i := tup.Len() - 1; i >= 0; i-- {
			f.dropCells(cells(tup.At(i).Type()))
		}
		return
	}
	if b, ok := t.(*types.Basic); ok && b.Kind() == types.Invalid {
		return
	}
	f.dropCells(cells(t))
}

func (f *fgen) dropCells(n int) {
	for ; n > 0; n-- {
		f.emit("drop")
	}
}

func (f *fgen) declStmt(s *ast.DeclStmt) {
	gd := s.Decl.(*ast.GenDecl)
	if gd.Tok != token.VAR {
		if gd.Tok == token.TYPE {
			for _, sp := range gd.Specs {
				f.errorf(sp.Pos(), "local type declarations are not supported: declare the type at package level")
			}
		}
		return // constants are folded
	}
	for _, sp := range gd.Specs {
		vs := sp.(*ast.ValueSpec)
		var vars []*types.Var
		for _, id := range vs.Names {
			v, _ := f.c.info.Defs[id].(*types.Var)
			vars = append(vars, v)
		}
		switch {
		case len(vs.Values) == 0:
			for _, v := range vars {
				if v == nil {
					continue
				}
				f.declare(v)
				f.zero(v)
			}
		case len(vs.Values) == len(vars):
			// evaluate all values, then declare and store in reverse
			for i, val := range vs.Values {
				f.valueFor(val, typeOrNil(vars[i]))
			}
			for i := len(vars) - 1; i >= 0; i-- {
				f.defineVar(vars[i], vs.Names[i])
			}
		default: // var a, b = f()
			f.expr(vs.Values[0])
			for i := len(vars) - 1; i >= 0; i-- {
				f.defineVar(vars[i], vs.Names[i])
			}
		}
	}
}

func typeOrNil(v *types.Var) types.Type {
	if v == nil {
		return nil
	}
	return v.Type()
}

// defineVar declares v and stores the value on the stack into it.
func (f *fgen) defineVar(v *types.Var, id *ast.Ident) {
	if v == nil || id.Name == "_" {
		var t types.Type
		if v != nil {
			t = v.Type()
		} else {
			t = f.c.typeOf(id)
		}
		f.dropCells(cells(t))
		return
	}
	f.declare(v)
	f.storeVar(v)
}

// zero sets a newly declared variable to its zero value.
func (f *fgen) zero(v *types.Var) {
	t := v.Type()
	if kindOf(t).inMemory() {
		f.emit(f.c.statics[v], itoa(sizeof(t)), "0", "fill")
		return
	}
	for i := 0; i < cells(t); i++ {
		f.emit("0")
	}
	f.storeVar(v)
}

// storeVar stores the value on the stack into a variable: frame slots,
// a package variable or static memory (by copying from an address).
func (f *fgen) storeVar(v *types.Var) {
	t := v.Type()
	if k, ok := f.slots[v]; ok {
		f.storeSlot(k, t)
		return
	}
	addr := f.c.globals[v]
	if addr == "" {
		addr = f.c.statics[v]
	}
	if kindOf(t).inMemory() {
		f.emit(addr, itoa(sizeof(t)), "move")
		return
	}
	f.emit(addr, storeWord(t))
}

// valueFor evaluates e for storing into a variable of type t; composite
// literals for in-memory types produce an address to copy from.
func (f *fgen) valueFor(e ast.Expr, t types.Type) {
	f.expr(e)
}

func (f *fgen) assignStmt(s *ast.AssignStmt) {
	switch s.Tok {
	case token.DEFINE:
		if len(s.Rhs) == len(s.Lhs) {
			for _, r := range s.Rhs {
				f.expr(r)
			}
		} else {
			f.expr(s.Rhs[0])
		}
		for i := len(s.Lhs) - 1; i >= 0; i-- {
			id := s.Lhs[i].(*ast.Ident)
			if v, ok := f.c.info.Defs[id].(*types.Var); ok && v != nil {
				f.defineVar(v, id)
				continue
			}
			// redeclared: plain assignment
			if id.Name == "_" {
				f.dropCells(cells(f.c.typeOf(s.Rhs[min(i, len(s.Rhs)-1)])))
				continue
			}
			f.storeTo(id)
		}
	case token.ASSIGN:
		f.assign(s.Lhs, s.Rhs)
	default:
		op := map[token.Token]token.Token{
			token.ADD_ASSIGN: token.ADD, token.SUB_ASSIGN: token.SUB, token.MUL_ASSIGN: token.MUL,
			token.QUO_ASSIGN: token.QUO, token.REM_ASSIGN: token.REM, token.AND_ASSIGN: token.AND,
			token.OR_ASSIGN: token.OR, token.XOR_ASSIGN: token.XOR, token.SHL_ASSIGN: token.SHL,
			token.SHR_ASSIGN: token.SHR, token.AND_NOT_ASSIGN: token.AND_NOT,
		}[s.Tok]
		f.opAssign(s.Lhs[0], op, s.Rhs[0], s.TokPos)
	}
}

// assign handles lhs... = rhs... with Go's order: index operands on the
// left, then the values, then the stores left to right.
func (f *fgen) assign(lhs, rhs []ast.Expr) {
	if len(lhs) == 1 && len(rhs) == 1 {
		f.assign1(lhs[0], rhs[0])
		return
	}
	// Go evaluates the index operands and pointer indirections on the left
	// before any assignment, so compute every address first (the stores
	// below could change a variable an address depends on)
	addrTemp := map[int]int{}
	save := f.next
	for i, l := range lhs {
		if f.isMemLvalue(l) {
			f.addr(l)
			k := f.allocSlots(1)
			f.emit(itoa(k), "l!")
			addrTemp[i] = k
		}
	}
	if len(rhs) == len(lhs) {
		for _, r := range rhs {
			f.expr(r)
			if t := f.c.typeOf(r); kindOf(t).inMemory() {
				// an array or struct value is an address: copy it, or a
				// swap would read a value it has already overwritten
				tmp := f.staticTemp(t, r.Pos())
				f.emit(tmp, itoa(sizeof(t)), "move", tmp)
			}
		}
	} else {
		f.expr(rhs[0])
	}
	// store in reverse; when the same variable appears twice, the last
	// assignment wins as in Go
	stored := map[types.Object]bool{}
	for i := len(lhs) - 1; i >= 0; i-- {
		l := ast.Unparen(lhs[i])
		t := f.c.typeOf(lhs[i])
		if id, ok := l.(*ast.Ident); ok {
			if id.Name == "_" {
				f.dropCells(cells(f.rhsType(rhs, i)))
				continue
			}
			obj := f.c.info.Uses[id]
			if stored[obj] {
				f.dropCells(cells(t))
				continue
			}
			stored[obj] = true
		}
		if k, ok := addrTemp[i]; ok {
			f.emit(itoa(k), "l@")
			f.storeMem(t)
			continue
		}
		f.storeTo(l)
	}
	f.next = save
}

func (f *fgen) rhsType(rhs []ast.Expr, i int) types.Type {
	if len(rhs) == 1 {
		if tup, ok := f.c.typeOf(rhs[0]).(*types.Tuple); ok {
			return tup.At(i).Type()
		}
		return f.c.typeOf(rhs[0])
	}
	return f.c.typeOf(rhs[i])
}

func (f *fgen) assign1(l, r ast.Expr) {
	l = ast.Unparen(l)
	if id, ok := l.(*ast.Ident); ok && id.Name == "_" {
		f.expr(r)
		f.drop(f.c.typeOf(r), r)
		return
	}
	t := f.c.typeOf(l)
	if f.isMemLvalue(l) && !f.pure(l) && !f.pure(r) {
		// Go order: the address, then the value
		f.addr(l)
		f.expr(r)
		if kindOf(t).inMemory() {
			f.emit("swap", itoa(sizeof(t)), "move")
			return
		}
		if cells(t) == 2 {
			f.emit("rot")
		} else {
			f.emit("swap")
		}
		f.emit(storeWord(t))
		return
	}
	f.expr(r)
	f.storeTo(l)
}

// storeTo stores the value on the stack into an lvalue.
func (f *fgen) storeTo(l ast.Expr) {
	l = ast.Unparen(l)
	t := f.c.typeOf(l)
	if id, ok := l.(*ast.Ident); ok {
		if v, ok := f.c.info.ObjectOf(id).(*types.Var); ok {
			if _, isSlot := f.slots[v]; isSlot || f.c.globals[v] != "" || f.c.statics[v] != "" {
				if kindOf(t) == kStruct && f.isParamStruct(v) {
					f.errorf(l.Pos(), "struct parameters are read-only")
				}
				f.storeVar(v)
				return
			}
		}
		f.errorf(l.Pos(), "cannot assign to %s", id.Name)
		return
	}
	f.addr(l)
	f.storeMem(t)
}

func (f *fgen) isParamStruct(v *types.Var) bool {
	if f.sig == nil {
		return false
	}
	for i := 0; i < f.sig.Params().Len(); i++ {
		if f.sig.Params().At(i) == v {
			return true
		}
	}
	return f.sig.Recv() == v
}

// storeMem stores the value below the address on the stack.
func (f *fgen) storeMem(t types.Type) {
	if kindOf(t).inMemory() {
		f.emit(itoa(sizeof(t)), "move")
		return
	}
	f.emit(storeWord(t))
}

// isMemLvalue reports whether l is stored through an address (not a
// frame slot or package variable).
func (f *fgen) isMemLvalue(l ast.Expr) bool {
	switch ast.Unparen(l).(type) {
	case *ast.IndexExpr, *ast.SelectorExpr, *ast.StarExpr:
		return true
	}
	return false
}

// opAssign handles x op= y, x++ and x--, evaluating x's address once.
func (f *fgen) opAssign(l ast.Expr, op token.Token, r ast.Expr, pos token.Pos) {
	l = ast.Unparen(l)
	t := f.c.typeOf(l)
	if id, ok := l.(*ast.Ident); ok {
		v, _ := f.c.info.ObjectOf(id).(*types.Var)
		if v == nil {
			f.errorf(pos, "cannot assign to %s", id.Name)
			return
		}
		f.loadVar(v)
		f.operand(r, t, op)
		f.binop(op, t, f.c.typeOf(r), pos)
		f.storeVar(v)
		return
	}
	f.addr(l)
	f.emit("dup")
	f.emit(loadWord(t))
	f.operand(r, t, op)
	f.binop(op, t, f.c.typeOf(r), pos)
	if cells(t) == 2 {
		f.emit("rot")
	} else {
		f.emit("swap")
	}
	f.emit(storeWord(t))
}

// operand evaluates the right side of x op= y. A literal 1 from ++ and --
// takes x's type; shift counts keep their own type.
func (f *fgen) operand(r ast.Expr, t types.Type, op token.Token) {
	if lit, ok := r.(*ast.BasicLit); ok && f.c.info.Types[r].Type == nil {
		f.constant(constant.MakeFromLiteral(lit.Value, lit.Kind, 0), t)
		return
	}
	f.expr(r)
}

func (f *fgen) loadVar(v *types.Var) {
	t := v.Type()
	if k, ok := f.slots[v]; ok {
		if kindOf(t) == kStruct && f.isParamStruct(v) {
			f.emit(itoa(k), "l@") // the address
			return
		}
		f.loadSlot(k, t)
		return
	}
	if a := f.c.statics[v]; a != "" {
		f.emit(a)
		return
	}
	a := f.c.globals[v]
	if a == "" {
		f.errorf(v.Pos(), "internal error: no storage for %s", v.Name())
		return
	}
	f.emit(a)
	if !kindOf(t).inMemory() {
		f.emit(loadWord(t))
	}
}

func (f *fgen) ifStmt(s *ast.IfStmt, last bool) {
	save := f.next
	if s.Init != nil {
		f.stmt(s.Init, false)
		f.nl()
	}
	f.cond(s.Cond)
	f.emit("if")
	f.depth++
	f.block(s.Body.List, last)
	f.depth--
	if s.Else != nil {
		f.nl()
		f.emit("else")
		f.depth++
		switch e := s.Else.(type) {
		case *ast.BlockStmt:
			f.block(e.List, last)
		case *ast.IfStmt:
			f.ifStmt(e, last)
		}
		f.depth--
	}
	f.nl()
	f.emit("fi")
	f.next = save
}

// cond evaluates a condition. Constant conditions are still emitted.
func (f *fgen) cond(e ast.Expr) { f.expr(e) }

func (f *fgen) pushBranch(label string, loop bool, pos token.Pos) *branchCtx {
	b := &branchCtx{label: label, loop: loop, cont: -1}
	b.brk = f.newLabel(pos)
	if loop {
		b.cont = f.newLabel(pos)
	}
	f.brk = append(f.brk, b)
	return b
}

func (f *fgen) popBranch(b *branchCtx) {
	f.brk = f.brk[:len(f.brk)-1]
	f.freeLabel(b.brk)
	f.freeLabel(b.cont)
}

// forStmt: init; begin cond zgoto0 B body zlabel C post again zlabel B
func (f *fgen) forStmt(s *ast.ForStmt, label string) {
	save := f.next
	if s.Init != nil {
		f.stmt(s.Init, false)
	}
	f.nl()
	b := f.pushBranch(label, true, s.Pos())
	f.emit("begin")
	f.depth++
	if s.Cond != nil {
		f.nl()
		f.cond(s.Cond)
		f.emit("zgoto0", itoa(b.brk))
	}
	f.block(s.Body.List, false)
	f.nl()
	f.emit("zlabel", itoa(b.cont))
	if s.Post != nil {
		f.stmt(s.Post, false)
	}
	f.depth--
	f.nl()
	f.emit("again", "zlabel", itoa(b.brk))
	f.popBranch(b)
	f.next = save
}

// rangeStmt handles range over an integer, array, slice or string.
func (f *fgen) rangeStmt(s *ast.RangeStmt, label string) {
	save := f.next
	xt := f.c.typeOf(s.X)
	k := kindOf(xt)
	var keyT types.Type = types.Typ[types.Int]
	if k.isInt() {
		keyT = xt
		if b, ok := xt.(*types.Basic); ok && b.Info()&types.IsUntyped != 0 {
			keyT = types.Typ[types.Int]
		}
	}
	// the collection: length in slot n (or a constant), address in slot a
	n, a := -1, -1
	var nConst string
	var arrAddr func()
	switch {
	case k.isInt():
		if cv := f.c.constOf(s.X); cv != nil {
			nConst = constText(cv, keyT)
		} else {
			f.expr(s.X)
			n = f.temp(keyT)
			f.storeSlot(n, keyT)
		}
	case k == kArray || k == kPointer: // an array or a pointer to one
		nConst = itoa(int(arrayOf(xt).Len()))
		if s.Value != nil {
			if f.pure(s.X) {
				x := s.X
				arrAddr = func() { f.expr(x) }
			} else {
				f.expr(s.X)
				a = f.allocSlots(1)
				f.emit(itoa(a), "l!")
			}
		}
	default: // slice, string
		f.expr(s.X)
		a = f.allocSlots(2)
		n = a + 1
		f.emit(itoa(a), "2l!")
	}
	if arrAddr == nil && a >= 0 {
		slot := a
		arrAddr = func() { f.emit(itoa(slot), "l@") }
	}
	pushLen := func() {
		if nConst != "" {
			f.emit(nConst)
		} else {
			f.loadSlot(n, keyT)
		}
	}

	// the hidden counter
	i := f.temp(keyT)
	f.zeroSlots(i, keyT)
	size := -1 // rune size slot for strings
	if k == kString {
		size = f.allocSlots(1)
	}

	// key and value variables
	var keyVar, valVar *types.Var
	if s.Tok == token.DEFINE {
		if id, ok := s.Key.(*ast.Ident); ok && id.Name != "_" {
			keyVar, _ = f.c.info.Defs[id].(*types.Var)
		}
		if id, ok := s.Value.(*ast.Ident); ok && id.Name != "_" {
			valVar, _ = f.c.info.Defs[id].(*types.Var)
		}
	}
	f.nl()
	b := f.pushBranch(label, true, s.Pos())
	f.emit("begin")
	f.depth++
	f.nl()
	f.loadSlot(i, keyT)
	pushLen()
	f.ltFor(keyT)
	f.emit("zgoto0", itoa(b.brk))
	// key
	if s.Key != nil && !isBlank(s.Key) {
		f.nl()
		f.loadSlot(i, keyT)
		if s.Tok == token.DEFINE {
			if keyVar != nil {
				f.declare(keyVar)
				f.storeVar(keyVar)
			} else {
				f.dropCells(cells(keyT))
			}
		} else {
			f.storeTo(s.Key)
		}
	}
	// value
	if k == kString {
		f.nl()
		arrAddr()
		f.loadSlot(n, keyT)
		f.loadSlot(i, keyT)
		f.emit("zrune", itoa(size), "l!")
		if s.Value != nil && !isBlank(s.Value) {
			f.valueStore(s, valVar)
		} else {
			f.emit("drop")
		}
	} else if s.Value != nil && !isBlank(s.Value) {
		f.nl()
		et := elemType(xt)
		arrAddr()
		f.loadSlot(i, keyT)
		f.scaleIndex(sizeof(et))
		f.emit("+")
		if !kindOf(et).inMemory() {
			f.emit(loadWord(et))
		}
		f.valueStore(s, valVar)
	}
	f.block(s.Body.List, false)
	f.nl()
	f.emit("zlabel", itoa(b.cont))
	f.loadSlot(i, keyT)
	if k == kString {
		f.emit(itoa(size), "l@")
	} else if keyT != nil && cells(keyT) == 2 {
		f.emit("1.")
	} else {
		f.emit("1")
	}
	if cells(keyT) == 2 {
		f.emit("d+")
	} else {
		f.emit("+")
	}
	f.storeSlot(i, keyT)
	f.depth--
	f.nl()
	f.emit("again", "zlabel", itoa(b.brk))
	f.popBranch(b)
	f.next = save
}

func (f *fgen) valueStore(s *ast.RangeStmt, valVar *types.Var) {
	if s.Tok == token.DEFINE {
		if valVar != nil {
			f.declare(valVar)
			f.storeVar(valVar)
		} else {
			f.dropCells(cells(f.c.typeOf(s.Value)))
		}
		return
	}
	f.storeTo(s.Value)
}

func isBlank(e ast.Expr) bool {
	id, ok := e.(*ast.Ident)
	return ok && id.Name == "_"
}

func (f *fgen) zeroSlots(k int, t types.Type) {
	if cells(t) == 2 {
		f.emit("0", "0")
	} else {
		f.emit("0")
	}
	f.storeSlot(k, t)
}

// ltFor emits < for an integer type.
func (f *fgen) ltFor(t types.Type) {
	f.emit(compareWord(token.LSS, kindOf(t)))
}

// scaleIndex multiplies an index by an element size.
func (f *fgen) scaleIndex(size int) {
	if size != 1 {
		f.emit(itoa(size), "*")
	}
}

// switchStmt lowers to a chain of ifs, or, with fallthrough, to a
// dispatch on labels followed by the case bodies in source order.
func (f *fgen) switchStmt(s *ast.SwitchStmt, label string, last bool) {
	save := f.next
	if s.Init != nil {
		f.stmt(s.Init, false)
	}
	tagSlot := -1
	var tagT types.Type
	if s.Tag != nil {
		tagT = f.c.typeOf(s.Tag)
		if b, ok := tagT.(*types.Basic); ok && b.Info()&types.IsUntyped != 0 {
			tagT = types.Default(tagT)
		}
		f.nl()
		f.expr(s.Tag)
		tagSlot = f.temp(tagT)
		f.storeSlot(tagSlot, tagT)
	}
	f.nl()
	b := f.pushBranch(label, false, s.Pos())
	clauses := s.Body.List
	hasFall := false
	for _, cl := range clauses {
		body := cl.(*ast.CaseClause).Body
		if len(body) > 0 {
			if br, ok := body[len(body)-1].(*ast.BranchStmt); ok && br.Tok == token.FALLTHROUGH {
				hasFall = true
			}
		}
	}
	matchClause := func(cl *ast.CaseClause) {
		// short-circuit unless every case value is side-effect free
		allPure := true
		for _, e := range cl.List {
			if !f.pure(e) {
				allPure = false
			}
		}
		for j, e := range cl.List {
			if tagSlot >= 0 {
				f.loadSlot(tagSlot, tagT)
				f.expr(e)
				f.compare(token.EQL, tagT)
			} else {
				f.cond(e)
			}
			if j > 0 && allPure {
				f.emit("|")
			} else if j < len(cl.List)-1 && !allPure {
				f.emit("if", "-1", "else")
			}
		}
		if !allPure {
			for j := 0; j < len(cl.List)-1; j++ {
				f.emit("fi")
			}
		}
	}
	if !hasFall {
		var cases []*ast.CaseClause
		var def *ast.CaseClause
		for _, c := range clauses {
			cl := c.(*ast.CaseClause)
			if cl.List == nil {
				def = cl
			} else {
				cases = append(cases, cl)
			}
		}
		// c1 if b1 else c2 if b2 else default fi fi
		var chain func(i int)
		chain = func(i int) {
			if i == len(cases) {
				if def != nil {
					f.block(def.Body, last)
				}
				return
			}
			f.nl()
			matchClause(cases[i])
			f.emit("if")
			f.depth++
			f.block(cases[i].Body, last)
			f.depth--
			if i+1 < len(cases) || def != nil {
				f.nl()
				f.emit("else")
				f.depth++
				chain(i + 1)
				f.depth--
			}
			f.nl()
			f.emit("fi")
		}
		chain(0)
	} else {
		labels := make([]int, len(clauses))
		for j := range clauses {
			labels[j] = f.newLabel(s.Pos())
		}
		defIdx := -1
		for j, c := range clauses {
			cl := c.(*ast.CaseClause)
			if cl.List == nil {
				defIdx = j
				continue
			}
			f.nl()
			matchClause(cl)
			f.emit("if", "zgoto", itoa(labels[j]), "fi")
		}
		f.nl()
		if defIdx >= 0 {
			f.emit("zgoto", itoa(labels[defIdx]))
		} else {
			f.emit("zgoto", itoa(b.brk))
		}
		for j, c := range clauses {
			cl := c.(*ast.CaseClause)
			f.nl()
			f.emit("zlabel", itoa(labels[j]))
			f.depth++
			body := cl.Body
			fall := false
			if len(body) > 0 {
				if br, ok := body[len(body)-1].(*ast.BranchStmt); ok && br.Tok == token.FALLTHROUGH {
					fall = true
					body = body[:len(body)-1]
				}
			}
			f.block(body, false)
			if !fall && j < len(clauses)-1 {
				f.nl()
				f.emit("zgoto", itoa(b.brk))
			}
			f.depth--
		}
		for _, l := range labels {
			f.freeLabel(l)
		}
	}
	f.nl()
	f.emit("zlabel", itoa(b.brk))
	f.popBranch(b)
	f.next = save
}

func (f *fgen) branchStmt(s *ast.BranchStmt) {
	switch s.Tok {
	case token.FALLTHROUGH:
		return // handled by switchStmt
	case token.BREAK, token.CONTINUE:
	default:
		f.errorf(s.Pos(), "unsupported %s", s.Tok)
		return
	}
	for i := len(f.brk) - 1; i >= 0; i-- {
		b := f.brk[i]
		if s.Label != nil {
			if b.label != s.Label.Name {
				continue
			}
		} else if s.Tok == token.CONTINUE && !b.loop {
			continue
		}
		if s.Tok == token.BREAK {
			f.emit("zgoto", itoa(b.brk))
		} else {
			f.emit("zgoto", itoa(b.cont))
		}
		return
	}
	f.errorf(s.Pos(), "%s is not in a loop or switch", s.Tok)
}

func (f *fgen) returnStmt(s *ast.ReturnStmt, last bool) {
	if f.fn == nil {
		f.errorf(s.Pos(), "return is not allowed here")
		return
	}
	switch {
	case len(s.Results) == 0 && len(f.results) > 0:
		for i, k := range f.results {
			f.loadSlot(k, f.sig.Results().At(i).Type())
		}
	case len(s.Results) == 1 && f.sig.Results().Len() > 1:
		f.expr(s.Results[0]) // return f() with several results
	default:
		for _, r := range s.Results {
			f.expr(r)
		}
	}
	if !last {
		f.emit(endlocalsTok, "exit")
	}
}

// initFunction generates z-init: package variables with non-constant
// initializers, in dependency order, then the init functions. Constant
// initializers are run at load time instead, so they are baked into ROM.
func (f *fgen) initFunction(globals []*types.Var) string {
	_ = globals
	for _, ini := range f.c.info.InitOrder {
		if len(ini.Lhs) == 1 && f.c.loadTimeInit(f.g, ini.Lhs[0], ini.Rhs) {
			continue
		}
		f.nl()
		if len(ini.Lhs) == 1 && ini.Lhs[0].Name() != "_" {
			f.storeInit(ini.Lhs[0], ini.Rhs, true)
			continue
		}
		f.expr(ini.Rhs)
		for i := len(ini.Lhs) - 1; i >= 0; i-- {
			v := ini.Lhs[i]
			if v.Name() == "_" {
				f.drop(v.Type(), nil)
				continue
			}
			f.storeVar(v)
		}
	}
	for _, fi := range f.c.inits {
		f.nl()
		f.emit(fi.word)
	}
	return f.assemble("z-init", "package initialization and init functions; call after loading")
}
