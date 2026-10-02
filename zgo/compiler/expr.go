package compiler

import (
	"fmt"
	"go/ast"
	"go/constant"
	"go/token"
	"go/types"
	"strconv"
	"strings"
)

// expr pushes the value of e. Values of array and struct type are pushed
// as their address.
func (f *fgen) expr(e ast.Expr) {
	tv := f.c.info.Types[e]
	if tv.IsNil() {
		for i := 0; i < max(1, cells(tv.Type)); i++ {
			f.emit("0")
		}
		return
	}
	if tv.Value != nil {
		f.constant(tv.Value, tv.Type)
		return
	}
	switch e := e.(type) {
	case *ast.ParenExpr:
		f.expr(e.X)
	case *ast.Ident:
		f.ident(e)
	case *ast.BinaryExpr:
		f.binary(e)
	case *ast.UnaryExpr:
		f.unary(e)
	case *ast.CallExpr:
		f.call(e)
	case *ast.IndexExpr:
		f.elemAddr(e)
		if et := f.c.typeOf(e); !kindOf(et).inMemory() {
			f.emit(loadWord(et))
		}
	case *ast.SliceExpr:
		f.sliceExpr(e)
	case *ast.SelectorExpr:
		f.selector(e)
	case *ast.StarExpr:
		f.expr(e.X)
		if t := f.c.typeOf(e); !kindOf(t).inMemory() {
			f.emit(loadWord(t))
		}
	case *ast.CompositeLit:
		f.compositeLit(e)
	default:
		f.errorf(e.Pos(), "unsupported expression")
	}
}

// constant pushes a constant of type t.
func (f *fgen) constant(v constant.Value, t types.Type) {
	f.emitWords(constText(v, t))
}

// emitWords emits space-separated words, keeping a z" string literal as
// one token.
func (f *fgen) emitWords(s string) {
	if strings.HasPrefix(s, `z" `) {
		f.emit(s)
		return
	}
	f.emit(strings.Fields(s)...)
}

// constText is the Forth text for a constant. Literals always carry their
// type: integers in decimal, 64-bit integers with a final dot, single
// floats with an e exponent and double floats with a d exponent.
func constText(v constant.Value, t types.Type) string {
	if t == nil {
		t = types.Default(constTypeOf(v))
	} else if b, ok := t.(*types.Basic); ok && b.Info()&types.IsUntyped != 0 {
		t = types.Default(t)
	}
	switch kindOf(t) {
	case kBool:
		if constant.BoolVal(v) {
			return "-1"
		}
		return "0"
	case kI8, kI16, kI32:
		x, _ := constant.Int64Val(constant.ToInt(v))
		return strconv.FormatInt(int64(int32(x)), 10)
	case kU8, kU16, kU32:
		x, ok := constant.Uint64Val(constant.ToInt(v))
		if !ok {
			y, _ := constant.Int64Val(constant.ToInt(v))
			x = uint64(y)
		}
		return strconv.FormatUint(uint64(uint32(x)), 10)
	case kI64:
		x, _ := constant.Int64Val(constant.ToInt(v))
		return strconv.FormatInt(x, 10) + "."
	case kU64:
		x, _ := constant.Uint64Val(constant.ToInt(v))
		return strconv.FormatUint(x, 10) + "."
	case kF32:
		x, _ := constant.Float32Val(constant.ToFloat(v))
		return strconv.FormatFloat(float64(x), 'e', -1, 32)
	case kF64:
		x, _ := constant.Float64Val(constant.ToFloat(v))
		return strings.Replace(strconv.FormatFloat(x, 'e', -1, 64), "e", "d", 1)
	case kString:
		return forthString(constant.StringVal(v))
	case kPointer:
		return "0"
	case kSlice:
		return "0 0"
	}
	return "0"
}

func constTypeOf(v constant.Value) types.Type {
	switch v.Kind() {
	case constant.Bool:
		return types.Typ[types.UntypedBool]
	case constant.String:
		return types.Typ[types.UntypedString]
	case constant.Float:
		return types.Typ[types.UntypedFloat]
	}
	return types.Typ[types.UntypedInt]
}

// forthString is a z" literal for s, escaping quotes, backslashes and
// control characters; other bytes, including UTF-8, are written as is.
func forthString(s string) string {
	var b strings.Builder
	b.WriteString(`z" `)
	for i := 0; i < len(s); i++ {
		c := s[i]
		switch {
		case c == '"':
			b.WriteString(`\"`)
		case c == '\\':
			b.WriteString(`\\`)
		case c == '\n':
			b.WriteString(`\n`)
		case c == '\t':
			b.WriteString(`\t`)
		case c == '\r':
			b.WriteString(`\r`)
		case c < 0x20 || c == 0x7f:
			fmt.Fprintf(&b, `\x%02x`, c)
		default:
			b.WriteByte(c)
		}
	}
	b.WriteByte('"')
	return b.String()
}

func (f *fgen) ident(id *ast.Ident) {
	switch o := f.c.info.Uses[id].(type) {
	case *types.Var:
		f.loadVar(o)
	default:
		f.errorf(id.Pos(), "unsupported use of %s", id.Name)
	}
}

func (f *fgen) binary(e *ast.BinaryExpr) {
	switch e.Op {
	case token.LAND:
		f.expr(e.X)
		f.emit("if")
		f.expr(e.Y)
		f.emit("else", "0", "fi")
		return
	case token.LOR:
		f.expr(e.X)
		f.emit("if", "-1", "else")
		f.expr(e.Y)
		f.emit("fi")
		return
	case token.EQL, token.NEQ, token.LSS, token.GTR, token.LEQ, token.GEQ:
		xt := f.c.typeOf(e.X)
		// slice == nil compares the address with 0
		xnil, ynil := f.c.info.Types[e.X].IsNil(), f.c.info.Types[e.Y].IsNil()
		if xnil || ynil {
			other := e.X
			if xnil {
				other = e.Y
			}
			xt = f.c.typeOf(other)
			if kindOf(xt) == kSlice {
				f.expr(other)
				f.emit("drop", "0", "=")
				if e.Op == token.NEQ {
					f.emit("0", "=")
				}
				return
			}
		}
		f.expr(e.X)
		f.expr(e.Y)
		f.compare(e.Op, xt)
		return
	}
	t := f.c.typeOf(e)
	f.expr(e.X)
	f.expr(e.Y)
	f.binop(e.Op, t, f.c.typeOf(e.Y), e.OpPos)
}

// compare emits a comparison of two values of type t.
func (f *fgen) compare(op token.Token, t types.Type) {
	if kindOf(t) == kString {
		switch op {
		case token.EQL:
			f.emit("str=")
		case token.NEQ:
			f.emit("str=", "0", "=")
		case token.LSS:
			f.emit("compare", "0", "<")
		case token.LEQ:
			f.emit("compare", "1", "<")
		case token.GTR:
			f.emit("compare", "0", ">")
		case token.GEQ:
			f.emit("compare", "-1", ">")
		}
		return
	}
	f.emitWords(compareWord(op, kindOf(t)))
}

func compareWord(op token.Token, k kind) string {
	var eq, lt, gt, le, ge string
	switch {
	case k.isInt32() && k.isSigned():
		eq, lt, gt, le, ge = "=", "<", ">", "<=", ">="
	case k.isInt32():
		eq, lt, gt, le, ge = "=", "u<", "u>", "u<=", "u>="
	case k == kI64:
		eq, lt, gt, le, ge = "d=", "d<", "d>", "d<=", "d>="
	case k == kU64:
		eq, lt, gt, le, ge = "d=", "du<", "du>", "du<=", "du>="
	case k == kF32:
		eq, lt, gt, le, ge = "f=", "f<", "f>", "f<=", "f>="
	case k == kF64:
		eq, lt, gt, le, ge = "df=", "df<", "df>", "df<=", "df>="
	default: // bool, pointer
		eq, lt, gt, le, ge = "=", "<", ">", "<=", ">="
	}
	switch op {
	case token.EQL:
		return eq
	case token.NEQ:
		if eq == "=" {
			return "!="
		}
		return eq + " 0 ="
	case token.LSS:
		return lt
	case token.GTR:
		return gt
	case token.LEQ:
		return le
	case token.GEQ:
		return ge
	}
	return "="
}

// binop emits an arithmetic or bitwise operator on two values of type t;
// ct is the type of the right operand (the count, for shifts).
func (f *fgen) binop(op token.Token, t, ct types.Type, pos token.Pos) {
	k := kindOf(t)
	pick := func(i32, u32, i64, u64, f32, f64 string) string {
		switch {
		case k.isInt32() && k.isSigned():
			return i32
		case k.isInt32():
			return u32
		case k == kI64:
			return i64
		case k == kU64:
			return u64
		case k == kF32:
			return f32
		case k == kF64:
			return f64
		}
		return ""
	}
	var w string
	switch op {
	case token.ADD:
		w = pick("+", "+", "d+", "d+", "f+", "df+")
	case token.SUB:
		w = pick("-", "-", "d-", "d-", "f-", "df-")
	case token.MUL:
		w = pick("*", "*", "ud*", "ud*", "f*", "df*")
	case token.QUO:
		w = pick("/", "u/", "d/", "ud/", "f/", "df/")
	case token.REM:
		w = pick("%", "umod", "dmod", "udmod", "", "")
	case token.AND:
		w = pick("&", "&", "d&", "d&", "", "")
	case token.OR:
		w = pick("|", "|", "d|", "d|", "", "")
	case token.XOR:
		w = pick("^", "^", "d^", "d^", "", "")
	case token.AND_NOT:
		w = pick("-1 ^ &", "-1 ^ &", "dinvert d&", "dinvert d&", "", "")
	case token.SHL, token.SHR:
		if ct != nil && kindOf(ct).isInt64() {
			f.emit("zcnt")
		}
		if op == token.SHL {
			w = pick("<<", "<<", "dlshift", "dlshift", "", "")
		} else {
			w = pick(">>", "rshift", "darshift", "drshift", "", "")
		}
	}
	if w == "" {
		f.errorf(pos, "operator %s is not supported on %s", op, typeName(t))
		return
	}
	f.emitWords(w)
	if k.isNarrow() {
		switch op {
		case token.ADD, token.SUB, token.MUL, token.SHL:
			f.narrow(k)
		case token.QUO:
			if k.isSigned() {
				f.narrow(k)
			}
		}
	}
}

// narrow re-narrows a result to an 8- or 16-bit type (ZGO_PLAN.md 5.4).
func (f *fgen) narrow(k kind) {
	switch k {
	case kI8:
		f.emit("24", "<<", "24", ">>")
	case kI16:
		f.emit("16", "<<", "16", ">>")
	case kU8:
		f.emit("255", "&")
	case kU16:
		f.emit("65535", "&")
	}
}

func (f *fgen) unary(e *ast.UnaryExpr) {
	t := f.c.typeOf(e)
	k := kindOf(t)
	switch e.Op {
	case token.AND:
		f.addr(e.X)
	case token.ADD:
		f.expr(e.X)
	case token.NOT:
		f.expr(e.X)
		f.emit("0", "=")
	case token.SUB:
		f.expr(e.X)
		switch {
		case k.isInt32():
			f.emit("negate")
			f.narrow(k)
		case k.isInt64():
			f.emit("dnegate")
		case k == kF32:
			f.emit("fnegate")
		case k == kF64:
			f.emit("dfnegate")
		}
	case token.XOR:
		f.expr(e.X)
		switch {
		case k.isInt32():
			f.emit("invert")
			if !k.isSigned() {
				f.narrow(k)
			}
		case k.isInt64():
			f.emit("dinvert")
		}
	default:
		f.errorf(e.Pos(), "operator %s is not supported", e.Op)
	}
}

// convert emits a conversion between types (ZGO_PLAN.md 5.3).
func (f *fgen) convert(from, to types.Type) {
	fk, tk := kindOf(from), kindOf(to)
	if fk == tk {
		return
	}
	switch {
	case tk == kString || tk == kSlice || tk == kPointer || tk == kBool:
		return
	case fk.isInt32() && tk.isInt32():
		if !fitsIn(fk, tk) {
			f.narrow(tk)
		}
	case fk.isInt32() && tk.isInt64():
		if fk.isSigned() {
			f.emit("s>d")
		} else {
			f.emit("0")
		}
	case fk.isInt64() && tk.isInt32():
		f.emit("drop")
		f.narrow(tk)
	case fk.isInt64() && tk.isInt64():
	case fk.isInt32() && tk == kF32:
		f.emit(map[bool]string{true: "s>f", false: "u>f"}[fk.isSigned()])
	case fk.isInt32() && tk == kF64:
		f.emit(map[bool]string{true: "s>df", false: "u>df"}[fk.isSigned()])
	case fk == kI64 && tk == kF32:
		f.emit("d>f")
	case fk == kU64 && tk == kF32:
		f.emit("ud>f")
	case fk == kI64 && tk == kF64:
		f.emit("d>df")
	case fk == kU64 && tk == kF64:
		f.emit("ud>df")
	case fk == kF32 && tk.isInt32():
		if tk == kU32 {
			f.emit("f>u")
		} else {
			f.emit("f>s")
			f.narrow(tk)
		}
	case fk == kF32 && tk == kI64:
		f.emit("f>d")
	case fk == kF32 && tk == kU64:
		f.emit("f>ud")
	case fk == kF64 && tk.isInt32():
		if tk == kU32 {
			f.emit("df>u")
		} else {
			f.emit("df>s")
			f.narrow(tk)
		}
	case fk == kF64 && tk == kI64:
		f.emit("df>d")
	case fk == kF64 && tk == kU64:
		f.emit("df>ud")
	case fk == kF32 && tk == kF64:
		f.emit("f>df")
	case fk == kF64 && tk == kF32:
		f.emit("df>f")
	}
}

// fitsIn reports whether every value of kind from is a valid value of kind
// to without re-narrowing. 32-bit targets just reinterpret the bits.
func fitsIn(from, to kind) bool {
	switch to {
	case kI32, kU32:
		return true
	case kI16:
		return from == kI8 || from == kU8
	case kU16:
		return from == kU8
	}
	return false
}

func (f *fgen) call(e *ast.CallExpr) {
	fun := ast.Unparen(e.Fun)
	if tv := f.c.info.Types[fun]; tv.IsType() {
		f.expr(e.Args[0])
		f.convert(f.c.typeOf(e.Args[0]), tv.Type)
		return
	}
	var obj types.Object
	var recv ast.Expr
	switch fn := fun.(type) {
	case *ast.Ident:
		obj = f.c.info.Uses[fn]
	case *ast.SelectorExpr:
		if sel := f.c.info.Selections[fn]; sel != nil {
			obj = sel.Obj()
			recv = fn.X
		} else {
			obj = f.c.info.Uses[fn.Sel]
		}
	}
	if b, ok := obj.(*types.Builtin); ok {
		f.builtin(b.Name(), e)
		return
	}
	fobj, ok := obj.(*types.Func)
	if !ok {
		f.errorf(e.Pos(), "function values are not supported")
		return
	}
	if name, ok := f.c.inlineFn[fobj]; ok {
		f.devCall(name, e)
		return
	}
	if recv != nil {
		f.receiver(recv, fobj.Type().(*types.Signature).Recv().Type())
	}
	for _, a := range e.Args {
		f.expr(a)
	}
	if word, ok := f.c.host[fobj]; ok {
		f.emitWords(word)
		return
	}
	if fi := f.c.funcs[fobj]; fi != nil {
		f.emit(fi.word)
		return
	}
	f.errorf(e.Pos(), "cannot call %s", fobj.Name())
}

// receiver pushes a method's receiver: an address for pointer receivers
// and for struct value receivers (passed by address, read-only), the value
// for other value receivers.
func (f *fgen) receiver(recv ast.Expr, rt types.Type) {
	isPtr := kindOf(f.c.typeOf(recv)) == kPointer
	switch {
	case kindOf(rt) == kPointer || kindOf(rt) == kStruct:
		if isPtr {
			f.expr(recv)
		} else {
			f.addr(recv)
		}
	case isPtr:
		f.expr(recv)
		f.emit(loadWord(rt))
	default:
		f.expr(recv)
	}
}

func (f *fgen) builtin(name string, e *ast.CallExpr) {
	switch name {
	case "len", "cap":
		// constant cases are folded by the type checker; an array operand
		// with a call in it must still be evaluated, as in Go
		x := e.Args[0]
		switch kindOf(f.c.typeOf(x)) {
		case kSlice, kString:
			f.expr(x)
			f.emit("nip")
		case kArray, kPointer:
			f.expr(x)
			f.emit("drop", itoa(int(arrayOf(f.c.typeOf(x)).Len())))
		default:
			f.errorf(e.Pos(), "%s of %s is not supported", name, typeName(f.c.typeOf(x)))
		}
	case "min", "max":
		t := f.c.typeOf(e)
		k := kindOf(t)
		var w string
		switch {
		case k.isInt32() && k.isSigned():
			w = name
		case k.isInt32():
			w = "u" + name
		case k == kI64:
			w = "d" + name
		case k == kU64:
			w = "du" + name
		case k == kF32:
			w = "f" + name
		case k == kF64:
			w = "df" + name
		}
		for i, a := range e.Args {
			f.expr(a)
			if i > 0 {
				f.emit(w)
			}
		}
	case "copy":
		dst, src := e.Args[0], e.Args[1]
		f.expr(dst)
		f.expr(src)
		f.emit(itoa(sizeof(elemType(f.c.typeOf(dst)))), "zcopy")
	case "print", "println":
		f.printBuiltin(name == "println", e)
	case "panic":
		// print the value, then abort the call (zf_eval returns
		// ZF_ABORT_USER), like an unrecovered panic without taking down
		// the firmware
		ffmt, pushes, ok := f.c.rewriteFormat("panic: %v\n", e.Args, e.Pos())
		if ok {
			f.pushFmtArgs(pushes)
			f.emit(forthString(ffmt), "fmt", "abort")
		}
	default:
		f.errorf(e.Pos(), "%s is not supported", name)
	}
}

// arrayOf is the array type of an array or a pointer to one.
func arrayOf(t types.Type) *types.Array {
	u := t.Underlying()
	if p, ok := u.(*types.Pointer); ok {
		u = p.Elem().Underlying()
	}
	return u.(*types.Array)
}

// index64 turns a 64-bit index into a single cell that fails the bounds
// check if it does not fit.
func (f *fgen) indexValue(e ast.Expr) {
	f.expr(e)
	if kindOf(f.c.typeOf(e)).isInt64() {
		f.emit("zcnt")
	}
}

// elemAddr pushes the address of an array, slice or string element.
func (f *fgen) elemAddr(e *ast.IndexExpr) {
	xt := f.c.typeOf(e.X)
	et := elemType(xt)
	size := sizeof(et)
	switch kindOf(xt) {
	case kArray, kPointer:
		at := xt.Underlying()
		if p, ok := at.(*types.Pointer); ok {
			at = p.Elem().Underlying()
		}
		n := int(at.(*types.Array).Len())
		f.expr(e.X)
		if cv := f.c.constOf(e.Index); cv != nil {
			i, _ := constant.Int64Val(constant.ToInt(cv))
			if off := int(i) * size; off != 0 {
				f.emit(itoa(off), "+")
			}
			return
		}
		f.indexValue(e.Index)
		if !f.c.opts.NoBounds {
			f.emit(itoa(n), "?bounds")
		}
	case kSlice, kString:
		f.expr(e.X)
		f.indexValue(e.Index)
		if f.c.opts.NoBounds {
			f.emit("nip")
		} else {
			f.emit("swap", "?bounds")
		}
	default:
		f.errorf(e.Pos(), "cannot index %s", typeName(xt))
		return
	}
	f.scaleIndex(size)
	f.emit("+")
}

func (f *fgen) sliceExpr(e *ast.SliceExpr) {
	xt := f.c.typeOf(e.X)
	et := elemType(xt)
	switch kindOf(xt) {
	case kArray, kPointer:
		at := xt.Underlying()
		if p, ok := at.(*types.Pointer); ok {
			at = p.Elem().Underlying()
		}
		f.expr(e.X)
		f.emit(itoa(int(at.(*types.Array).Len())))
	default:
		f.expr(e.X)
	}
	if e.Low == nil && e.High == nil {
		return
	}
	if e.Low == nil {
		f.emit("0")
	} else {
		f.indexValue(e.Low)
	}
	if e.High == nil {
		f.emit("over")
	} else {
		f.indexValue(e.High)
	}
	f.emit(itoa(sizeof(et)), "zslice")
}

func (f *fgen) selector(e *ast.SelectorExpr) {
	sel := f.c.info.Selections[e]
	if sel == nil || sel.Kind() != types.FieldVal {
		if v, ok := f.c.info.Uses[e.Sel].(*types.Var); ok {
			f.loadVar(v)
			return
		}
		f.errorf(e.Pos(), "unsupported selector %s", e.Sel.Name)
		return
	}
	f.fieldAddr(e)
	if t := f.c.typeOf(e); !kindOf(t).inMemory() {
		f.emit(loadWord(t))
	}
}

// fieldAddr pushes the address of a struct field.
func (f *fgen) fieldAddr(e *ast.SelectorExpr) {
	sel := f.c.info.Selections[e]
	f.expr(e.X) // a pointer, or a struct in memory: either way an address
	st := f.c.typeOf(e.X).Underlying()
	if p, ok := st.(*types.Pointer); ok {
		st = p.Elem().Underlying()
	}
	off := layoutOf(st.(*types.Struct)).offsets[sel.Index()[0]]
	if off != 0 {
		f.emit(itoa(off), "+")
	}
}

// addr pushes the address of an addressable expression.
func (f *fgen) addr(e ast.Expr) {
	switch x := ast.Unparen(e).(type) {
	case *ast.Ident:
		v, _ := f.c.info.Uses[x].(*types.Var)
		if v == nil {
			f.errorf(x.Pos(), "cannot take the address of %s", x.Name)
			return
		}
		if a := f.c.globals[v]; a != "" {
			f.emit(a)
			return
		}
		if a := f.c.statics[v]; a != "" {
			f.emit(a)
			return
		}
		if k, ok := f.slots[v]; ok && kindOf(v.Type()) == kStruct {
			f.emit(itoa(k), "l@")
			return
		}
		f.errorf(x.Pos(), "cannot take the address of local variable %s: locals live in the call frame on the return stack; use a package-level variable", x.Name)
	case *ast.IndexExpr:
		f.elemAddr(x)
	case *ast.SelectorExpr:
		f.fieldAddr(x)
	case *ast.StarExpr:
		f.expr(x.X)
	case *ast.CompositeLit:
		f.compositeLit(x)
	default:
		f.errorf(e.Pos(), "cannot take the address of this expression")
	}
}

// compositeLit builds an array, struct or slice literal in static storage
// and pushes its address (addr len for a slice).
func (f *fgen) compositeLit(e *ast.CompositeLit) {
	t := f.c.typeOf(e)
	if kindOf(t) == kSlice {
		n := sliceLitLen(f.c, e)
		at := types.NewArray(elemType(t), int64(n))
		base := f.staticTemp(at, e.Pos())
		f.fillLit(base, 0, at, e, true)
		f.emit(base, itoa(n))
		return
	}
	base := f.staticTemp(t, e.Pos())
	f.fillLit(base, 0, t, e, true)
	f.emit(base)
}

func sliceLitLen(c *compiler, e *ast.CompositeLit) int {
	n, idx := 0, 0
	for _, el := range e.Elts {
		if kv, ok := el.(*ast.KeyValueExpr); ok {
			k, _ := constant.Int64Val(constant.ToInt(c.constOf(kv.Key)))
			idx = int(k)
		}
		idx++
		n = max(n, idx)
	}
	return n
}

// fillLit stores a composite literal's elements at base+off. With zero
// set, the memory is cleared first so omitted elements are zero.
func (f *fgen) fillLit(base string, off int, t types.Type, e *ast.CompositeLit, zero bool) {
	if zero && !litCoversAll(f.c, t, e) {
		f.nl()
		f.emit(base)
		if off != 0 {
			f.emit(itoa(off), "+")
		}
		f.emit(itoa(sizeof(t)), "0", "fill")
	}
	store := func(val ast.Expr, et types.Type, at int) {
		if lit, ok := val.(*ast.CompositeLit); ok && kindOf(et) != kSlice {
			f.fillLit(base, at, et, lit, false)
			return
		}
		if u, ok := val.(*ast.UnaryExpr); ok && u.Op == token.AND {
			if lit, ok := u.X.(*ast.CompositeLit); ok {
				f.compositeLit(lit)
				f.storeAt(base, at, et)
				return
			}
		}
		f.expr(val)
		f.storeAt(base, at, et)
	}
	switch u := t.Underlying().(type) {
	case *types.Array:
		es := sizeof(u.Elem())
		idx := 0
		for _, el := range e.Elts {
			if kv, ok := el.(*ast.KeyValueExpr); ok {
				k, _ := constant.Int64Val(constant.ToInt(f.c.constOf(kv.Key)))
				idx = int(k)
				el = kv.Value
			}
			store(el, u.Elem(), off+idx*es)
			idx++
		}
	case *types.Struct:
		l := layoutOf(u)
		for i, el := range e.Elts {
			fi := i
			if kv, ok := el.(*ast.KeyValueExpr); ok {
				name := kv.Key.(*ast.Ident).Name
				for j := 0; j < u.NumFields(); j++ {
					if u.Field(j).Name() == name {
						fi = j
					}
				}
				el = kv.Value
			}
			store(el, u.Field(fi).Type(), off+l.offsets[fi])
		}
	}
}

// litCoversAll reports whether a literal sets every byte (so no clearing
// is needed): an array literal with every element, without keys.
func litCoversAll(c *compiler, t types.Type, e *ast.CompositeLit) bool {
	a, ok := t.Underlying().(*types.Array)
	if !ok || int64(len(e.Elts)) != a.Len() {
		return false
	}
	for _, el := range e.Elts {
		if _, ok := el.(*ast.KeyValueExpr); ok {
			return false
		}
		if lit, ok := el.(*ast.CompositeLit); ok && !litCoversAll(c, a.Elem(), lit) {
			return false
		}
		if _, ok := a.Elem().Underlying().(*types.Struct); ok {
			return false
		}
	}
	return true
}

// storeAt stores the value on the stack at base+off.
func (f *fgen) storeAt(base string, off int, t types.Type) {
	f.emit(base)
	if off != 0 {
		f.emit(itoa(off), "+")
	}
	f.storeMem(t)
}

// pure reports whether evaluating e has no side effects: no calls other
// than conversions and side-effect-free builtins, and no composite
// literals. Index expressions may abort but are otherwise pure.
func (f *fgen) pure(e ast.Expr) bool {
	ok := true
	ast.Inspect(e, func(n ast.Node) bool {
		switch n := n.(type) {
		case *ast.CallExpr:
			if tv := f.c.info.Types[ast.Unparen(n.Fun)]; tv.IsType() {
				return true
			}
			if id, isId := ast.Unparen(n.Fun).(*ast.Ident); isId {
				if b, isB := f.c.info.Uses[id].(*types.Builtin); isB {
					switch b.Name() {
					case "len", "cap", "min", "max":
						return true
					}
				}
			}
			ok = false
		case *ast.CompositeLit:
			ok = false
		}
		return ok
	})
	return ok
}
