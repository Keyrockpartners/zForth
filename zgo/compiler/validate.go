package compiler

import (
	"go/ast"
	"go/constant"
	"go/token"
	"go/types"
	"strconv"
)

// Messages for constructs outside the subset (ZGO_PLAN.md 4.12).
const (
	msgGoroutines = "goroutines and channels are not supported"
	msgAlloc      = "no dynamic allocation: use a fixed array (var buf [64]byte)"
	msgMaps       = "maps are not supported: " + msgAlloc
	msgConcat     = "string concatenation allocates: use dev.Format into a byte array"
	msgClosures   = "closures and function values are not supported"
	msgInterfaces = "interfaces are not supported"
	msgGenerics   = "generics are not supported"
	msgLocalArray = "local arrays and structs are not allowed in recursive functions: declare them at package level"
	msgArrayParam = "arrays are not passed by value: pass a slice, f(a[:])"
	msgFormat     = "format strings must be constants"
)

// validateSyntax rejects constructs that need no type information, before
// type checking, so the messages are not buried under type errors.
func (c *compiler) validateSyntax() {
	for _, f := range c.files {
		if f.Name.Name != "main" {
			c.errorf(f.Name.Pos(), "ZGo programs must be package main, not package %s", f.Name.Name)
		}
		for _, imp := range f.Imports {
			path, _ := strconv.Unquote(imp.Path.Value)
			switch path {
			case "dev":
				if imp.Name != nil && imp.Name.Name != "dev" {
					c.errorf(imp.Pos(), "import \"dev\" without renaming it")
				}
			case "fmt":
				c.errorf(imp.Pos(), "package fmt is not available: use dev.Printf to print and dev.Format to format into a byte array")
			default:
				c.errorf(imp.Pos(), "package %q is not available: ZGo programs can only import \"dev\"", path)
			}
		}
		ast.Inspect(f, c.syntaxNode)
	}
}

func (c *compiler) syntaxNode(n ast.Node) bool {
	switch n := n.(type) {
	case *ast.FuncDecl:
		if n.Type.TypeParams != nil {
			c.errorf(n.Pos(), msgGenerics)
		}
		c.checkParams(n.Type)
		// don't treat the declaration's own FuncType as a function value
		if n.Recv != nil {
			ast.Inspect(n.Recv, c.syntaxNode)
		}
		ast.Inspect(n.Type.Params, c.syntaxNode)
		if n.Type.Results != nil {
			ast.Inspect(n.Type.Results, c.syntaxNode)
		}
		if n.Body != nil {
			ast.Inspect(n.Body, c.syntaxNode)
		}
		return false
	case *ast.TypeSpec:
		if n.TypeParams != nil {
			c.errorf(n.Pos(), msgGenerics)
		}
	case *ast.GoStmt:
		c.errorf(n.Pos(), msgGoroutines)
	case *ast.SelectStmt, *ast.SendStmt, *ast.ChanType:
		c.errorf(n.Pos(), msgGoroutines)
	case *ast.UnaryExpr:
		if n.Op == token.ARROW {
			c.errorf(n.Pos(), msgGoroutines)
		}
	case *ast.DeferStmt:
		c.errorf(n.Pos(), "defer is not supported")
	case *ast.MapType:
		c.errorf(n.Pos(), msgMaps)
	case *ast.FuncLit:
		c.errorf(n.Pos(), msgClosures)
		return false
	case *ast.FuncType:
		c.errorf(n.Pos(), msgClosures)
		return false
	case *ast.InterfaceType:
		c.errorf(n.Pos(), msgInterfaces)
		return false
	case *ast.TypeAssertExpr, *ast.TypeSwitchStmt:
		c.errorf(n.Pos(), msgInterfaces)
		return false
	case *ast.BranchStmt:
		if n.Tok == token.GOTO {
			c.errorf(n.Pos(), "goto is not supported: use a loop with break or continue")
		}
	case *ast.LabeledStmt:
		switch n.Stmt.(type) {
		case *ast.ForStmt, *ast.RangeStmt, *ast.SwitchStmt:
		default:
			c.errorf(n.Pos(), "labels are only supported on for and switch statements")
		}
	case *ast.SliceExpr:
		if n.Slice3 {
			c.errorf(n.Pos(), "three-index slices are not supported: ZGo slices have no capacity")
		}
	case *ast.StructType:
		for _, fld := range n.Fields.List {
			if len(fld.Names) == 0 {
				c.errorf(fld.Pos(), "embedded fields are not supported: give the field a name")
			}
		}
	case *ast.IndexListExpr:
		c.errorf(n.Pos(), msgGenerics)
	}
	return true
}

func (c *compiler) checkParams(ft *ast.FuncType) {
	if ft.Params == nil {
		return
	}
	for _, p := range ft.Params.List {
		if _, ok := p.Type.(*ast.Ellipsis); ok {
			c.errorf(p.Pos(), "variadic functions are not supported (only dev.Printf and dev.Format take ...)")
		}
	}
}

// validateTypes rejects constructs that need types: allocation builtins,
// string concatenation, unsupported types and conversions.
func (c *compiler) validateTypes() {
	for _, f := range c.files {
		ast.Inspect(f, func(n ast.Node) bool {
			switch n := n.(type) {
			case *ast.CallExpr:
				c.checkCall(n)
			case *ast.BinaryExpr:
				if n.Op == token.ADD && kindOf(c.typeOf(n)) == kString && c.constOf(n) == nil {
					c.errorf(n.OpPos, msgConcat)
				}
				if n.Op == token.EQL || n.Op == token.NEQ {
					if k := kindOf(c.typeOf(n.X)); k.inMemory() {
						c.errorf(n.OpPos, "comparing arrays or structs with == is not supported: compare the elements or fields")
					}
				}
			case *ast.AssignStmt:
				if n.Tok == token.ADD_ASSIGN && kindOf(c.typeOf(n.Lhs[0])) == kString {
					c.errorf(n.TokPos, msgConcat)
				}
			case *ast.Ident:
				c.checkIdentUse(n)
			case *ast.SelectorExpr:
				if sel := c.info.Selections[n]; sel != nil {
					switch sel.Kind() {
					case types.MethodVal:
						if !c.isCalled(n) {
							c.errorf(n.Pos(), msgClosures)
						}
					case types.MethodExpr:
						c.errorf(n.Pos(), msgClosures)
					case types.FieldVal:
						if len(sel.Index()) > 1 {
							c.errorf(n.Pos(), "embedded fields are not supported")
						}
					}
				}
			case *ast.RangeStmt:
				switch kindOf(c.typeOf(n.X)) {
				case kI32, kU32, kI8, kI16, kU8, kU16, kI64, kU64, kArray, kSlice, kString:
				case kPointer:
					if _, ok := c.typeOf(n.X).Underlying().(*types.Pointer).Elem().Underlying().(*types.Array); !ok {
						c.errorf(n.X.Pos(), "cannot range over %s", typeName(c.typeOf(n.X)))
					}
				default:
					c.errorf(n.X.Pos(), "cannot range over %s", typeName(c.typeOf(n.X)))
				}
			}
			return true
		})
	}
	for id, obj := range c.info.Defs {
		if obj == nil {
			continue
		}
		switch o := obj.(type) {
		case *types.Var:
			c.checkType(id.Pos(), o.Type())
		case *types.TypeName:
			c.checkType(id.Pos(), o.Type())
		case *types.Func:
			sig := o.Type().(*types.Signature)
			for i := 0; i < sig.Params().Len(); i++ {
				p := sig.Params().At(i)
				if kindOf(p.Type()) == kArray {
					c.errorf(p.Pos(), msgArrayParam)
				}
			}
			for i := 0; i < sig.Results().Len(); i++ {
				r := sig.Results().At(i)
				switch kindOf(r.Type()) {
				case kArray:
					c.errorf(r.Pos(), "functions cannot return arrays: fill a caller's array through a slice")
				case kStruct:
					c.errorf(r.Pos(), "functions cannot return structs: return a pointer, or fill a caller's struct through a pointer")
				}
			}
		}
	}
}

// checkType rejects types with no zForth representation.
func (c *compiler) checkType(pos token.Pos, t types.Type) {
	c.checkTypeSeen(pos, t, map[types.Type]bool{})
}

func (c *compiler) checkTypeSeen(pos token.Pos, t types.Type, seen map[types.Type]bool) {
	if seen[t] {
		return
	}
	seen[t] = true
	switch u := t.Underlying().(type) {
	case *types.Basic:
		switch u.Kind() {
		case types.Complex64, types.Complex128, types.UntypedComplex:
			c.errorf(pos, "complex numbers are not supported")
		case types.UnsafePointer:
			c.errorf(pos, "unsafe.Pointer is not supported")
		}
	case *types.Array:
		c.checkTypeSeen(pos, u.Elem(), seen)
	case *types.Slice:
		c.checkTypeSeen(pos, u.Elem(), seen)
	case *types.Pointer:
		c.checkTypeSeen(pos, u.Elem(), seen)
	case *types.Struct:
		for i := 0; i < u.NumFields(); i++ {
			c.checkTypeSeen(pos, u.Field(i).Type(), seen)
		}
	case *types.Map:
		c.errorf(pos, msgMaps)
	case *types.Chan:
		c.errorf(pos, msgGoroutines)
	case *types.Signature:
		c.errorf(pos, msgClosures)
	case *types.Interface:
		c.errorf(pos, msgInterfaces)
	}
}

func (c *compiler) checkCall(call *ast.CallExpr) {
	fun := ast.Unparen(call.Fun)
	if tv, ok := c.info.Types[fun]; ok && tv.IsType() {
		c.checkConversion(call, tv.Type)
		return
	}
	id, ok := fun.(*ast.Ident)
	if !ok {
		return
	}
	b, ok := c.info.Uses[id].(*types.Builtin)
	if !ok {
		return
	}
	switch b.Name() {
	case "append", "make", "new":
		c.errorf(call.Pos(), "%s: %s", b.Name(), msgAlloc)
	case "recover":
		c.errorf(call.Pos(), "recover is not supported: a panic aborts the call the firmware made")
	case "complex", "real", "imag":
		c.errorf(call.Pos(), "complex numbers are not supported")
	case "delete":
		c.errorf(call.Pos(), msgMaps)
	case "close":
		c.errorf(call.Pos(), msgGoroutines)
	case "clear":
		c.errorf(call.Pos(), "clear is not supported: assign the zero value or loop over the elements")
	case "cap":
		if kindOf(c.typeOf(call.Args[0])) == kSlice {
			c.errorf(call.Pos(), "cap of a slice is not supported: ZGo slices have no capacity, use len")
		}
	case "min", "max":
		for _, a := range call.Args {
			if kindOf(c.typeOf(a)) == kString {
				c.errorf(call.Pos(), "%s of strings is not supported", b.Name())
				break
			}
		}
	}
}

func (c *compiler) checkConversion(call *ast.CallExpr, to types.Type) {
	if len(call.Args) != 1 {
		return
	}
	from := c.typeOf(call.Args[0])
	tk, fk := kindOf(to), kindOf(from)
	switch {
	case tk == kString && fk.isInt():
		if c.constOf(call) == nil {
			c.errorf(call.Pos(), "string(rune) is not supported: it would allocate")
		}
	case tk == kString && fk == kSlice:
		if kindOf(elemType(from)) != kU8 {
			c.errorf(call.Pos(), "only string(byteSlice) is supported")
		}
	case tk == kSlice && fk == kString:
		c.errorf(call.Pos(), "converting a string to a slice allocates: copy it into a byte array with copy(buf[:], s)")
	}
}

// checkIdentUse rejects functions used as values.
func (c *compiler) checkIdentUse(id *ast.Ident) {
	obj, ok := c.info.Uses[id].(*types.Func)
	if !ok {
		return
	}
	if !c.isCalled(id) {
		c.errorf(id.Pos(), msgClosures)
	}
	_ = obj
}

// isCalled reports whether e is the function of a call expression.
func (c *compiler) isCalled(e ast.Expr) bool {
	p := c.parentOf(e)
	for {
		if pe, ok := p.(*ast.ParenExpr); ok {
			e = pe
			p = c.parentOf(pe)
			continue
		}
		break
	}
	if sel, ok := p.(*ast.SelectorExpr); ok && sel.Sel == e {
		return c.isCalled(sel)
	}
	call, ok := p.(*ast.CallExpr)
	return ok && call.Fun == e
}

func (c *compiler) parentOf(n ast.Node) ast.Node {
	if c.parents == nil {
		c.parents = map[ast.Node]ast.Node{}
		for _, f := range c.files {
			var stack []ast.Node
			ast.Inspect(f, func(m ast.Node) bool {
				if m == nil {
					stack = stack[:len(stack)-1]
					return false
				}
				if len(stack) > 0 {
					c.parents[m] = stack[len(stack)-1]
				}
				stack = append(stack, m)
				return true
			})
		}
	}
	return c.parents[n]
}

func (c *compiler) typeOf(e ast.Expr) types.Type {
	if tv, ok := c.info.Types[e]; ok && tv.Type != nil {
		return tv.Type
	}
	if id, ok := e.(*ast.Ident); ok {
		if obj := c.info.ObjectOf(id); obj != nil {
			return obj.Type()
		}
	}
	return types.Typ[types.Invalid]
}

func (c *compiler) constOf(e ast.Expr) constant.Value {
	if tv, ok := c.info.Types[e]; ok {
		return tv.Value
	}
	return nil
}
