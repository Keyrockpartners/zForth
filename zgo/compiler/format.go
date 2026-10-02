package compiler

import (
	"go/ast"
	"go/constant"
	"go/token"
	"go/types"
	"strings"
)

// maxFmtCells is the most argument cells one fmt call may take (the Linux
// host's limit; devices must allow at least as many).
const maxFmtCells = 16

// fmtArg is one argument of a rewritten format: how to push it.
type fmtArg struct {
	expr ast.Expr
	bool bool // push "true"/"false" with zbool
}

// rewriteFormat turns a Go format string into a zForth fmt string, choosing
// each placeholder from its argument's static type (ZGO_PLAN.md 4.8).
func (c *compiler) rewriteFormat(format string, args []ast.Expr, pos token.Pos) (string, []fmtArg, bool) {
	var out strings.Builder
	var pushes []fmtArg
	argi := 0
	ok := true
	for i := 0; i < len(format); i++ {
		ch := format[i]
		if ch != '%' {
			out.WriteByte(ch)
			continue
		}
		// %[flags][width][.prec]verb
		j := i + 1
		for j < len(format) && strings.IndexByte("+-# 0", format[j]) >= 0 {
			j++
		}
		for j < len(format) && format[j] >= '0' && format[j] <= '9' {
			j++
		}
		if j < len(format) && format[j] == '.' {
			j++
			for j < len(format) && format[j] >= '0' && format[j] <= '9' {
				j++
			}
		}
		if j >= len(format) {
			c.errorf(pos, "format %q ends with an incomplete verb", format)
			return "", nil, false
		}
		spec := format[i+1 : j]
		verb := format[j]
		i = j
		if strings.ContainsAny(spec, "*[") || verb == '*' || verb == '[' {
			c.errorf(pos, "argument indexes and * widths are not supported in format strings")
			ok = false
			continue
		}
		if verb == '%' {
			out.WriteString("%%")
			continue
		}
		if argi >= len(args) {
			c.errorf(pos, "format %%%c has no matching argument", verb)
			ok = false
			continue
		}
		arg := args[argi]
		argi++
		t := c.typeOf(arg)
		if b, isB := t.(*types.Basic); isB && b.Info()&types.IsUntyped != 0 {
			t = types.Default(t)
		}
		k := kindOf(t)
		bad := func() {
			c.errorf(arg.Pos(), "format %%%c does not take %s; supported verbs: %s", verb, typeName(t), supportedVerbs)
			ok = false
		}
		var ph string
		push := fmtArg{expr: arg}
		switch verb {
		case 'd':
			switch {
			case k.isInt32() && k.isSigned():
				ph = "d"
			case k.isInt32():
				ph = "u"
			case k == kI64:
				ph = "ld"
			case k == kU64:
				ph = "lu"
			default:
				bad()
			}
		case 'x', 'X':
			switch {
			case k.isInt32():
				ph = string(verb)
			case k.isInt64():
				ph = "l" + string(verb)
			default:
				bad()
			}
		case 'c':
			if k.isInt32() {
				ph = "c"
			} else {
				bad()
			}
		case 'f', 'F', 'e', 'E', 'g', 'G':
			v := strings.ToLower(string(verb))
			if verb == 'E' || verb == 'G' {
				v = string(verb)
			}
			switch k {
			case kF32:
				ph = v
			case kF64:
				ph = "l" + v
			default:
				bad()
			}
		case 's':
			switch {
			case k == kString:
				ph = "s"
			case k == kSlice && kindOf(elemType(t)) == kU8:
				ph = "s"
			default:
				bad()
			}
		case 't':
			if k == kBool {
				ph = "s"
				push.bool = true
			} else {
				bad()
			}
		case 'v':
			switch {
			case k.isInt32() && k.isSigned():
				ph = "d"
			case k.isInt32():
				ph = "u"
			case k == kI64:
				ph = "ld"
			case k == kU64:
				ph = "lu"
			case k == kF32:
				ph = "g"
			case k == kF64:
				ph = "lg"
			case k == kString:
				ph = "s"
			case k == kSlice && kindOf(elemType(t)) == kU8:
				ph = "s"
			case k == kBool:
				ph = "s"
				push.bool = true
			default:
				bad()
			}
		default:
			c.errorf(arg.Pos(), "format verb %%%c is not supported; supported verbs: %s", verb, supportedVerbs)
			ok = false
			continue
		}
		if ph == "" {
			continue
		}
		// C wants the precision for %s as a byte count, Go as runes; the
		// flags and width carry over
		if ph == "s" && strings.Contains(spec, "#") {
			spec = strings.ReplaceAll(spec, "#", "")
		}
		out.WriteString("%" + spec + ph)
		pushes = append(pushes, push)
	}
	if argi < len(args) {
		c.errorf(args[argi].Pos(), "too many arguments for format %q", format)
		ok = false
	}
	cellCount := 0
	for _, p := range pushes {
		if p.bool {
			cellCount += 2
		} else {
			cellCount += cells(c.typeOf(p.expr))
		}
	}
	if cellCount > maxFmtCells {
		c.errorf(pos, "format arguments take %d cells, more than the %d one call can print: split it into several calls", cellCount, maxFmtCells)
		ok = false
	}
	return out.String(), pushes, ok
}

const supportedVerbs = "%d %x %X %c %f %e %E %g %G %s %v %t %%"

func (f *fgen) pushFmtArgs(pushes []fmtArg) {
	for _, p := range pushes {
		f.expr(p.expr)
		if p.bool {
			f.emit("zbool")
		}
	}
}

// devCall handles dev.Printf and dev.Format.
func (f *fgen) devCall(name string, e *ast.CallExpr) {
	switch name {
	case "Printf":
		format, ok := f.constFormat(e.Args[0])
		if !ok {
			return
		}
		ffmt, pushes, ok := f.c.rewriteFormat(format, e.Args[1:], e.Pos())
		if !ok {
			return
		}
		f.pushFmtArgs(pushes)
		f.emit(forthString(ffmt), "fmt")
	case "Format":
		format, ok := f.constFormat(e.Args[1])
		if !ok {
			return
		}
		ffmt, pushes, ok := f.c.rewriteFormat(format, e.Args[2:], e.Pos())
		if !ok {
			return
		}
		f.expr(e.Args[0])
		f.pushFmtArgs(pushes)
		f.emit(forthString(ffmt), "fmt-buf")
	default:
		f.errorf(e.Pos(), "dev.%s is not supported", name)
	}
}

func (f *fgen) constFormat(e ast.Expr) (string, bool) {
	cv := f.c.constOf(e)
	if cv == nil || cv.Kind() != constant.String {
		f.errorf(e.Pos(), msgFormat)
		return "", false
	}
	return constant.StringVal(cv), true
}

// printBuiltin handles print and println with %v formatting; println puts
// spaces between the operands and a newline at the end.
func (f *fgen) printBuiltin(ln bool, e *ast.CallExpr) {
	var format strings.Builder
	for i, a := range e.Args {
		if ln && i > 0 {
			format.WriteByte(' ')
		}
		format.WriteString("%v")
		_ = a
	}
	if ln {
		format.WriteByte('\n')
	}
	ffmt, pushes, ok := f.c.rewriteFormat(format.String(), e.Args, e.Pos())
	if !ok {
		return
	}
	f.pushFmtArgs(pushes)
	f.emit(forthString(ffmt), "fmt")
}
