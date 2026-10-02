package compiler

import (
	"go/ast"
	"go/token"
	"go/types"
	"strconv"
	"strings"
)

// inlineWords replaces calls to short Forth words with their primitive
// bodies. A word call costs a call and a return on top of its body, so
// inlining roughly halves the work for these on a device.
var inlineWords = map[string][]string{
	"@c": {"1", "@@"}, "!c": {"1", "!!"},
	"@u8": {"2", "@@"}, "!u8": {"2", "!!"},
	"@u16": {"3", "@@"}, "!u16": {"3", "!!"},
	"@s8": {"5", "@@"}, "@s16": {"6", "@@"},
	">": {"swap", "<"}, "u>": {"swap", "u<"},
	"!=":     {"=", "0", "="},
	"<=":     {"swap", "<", "0", "="},
	">=":     {"<", "0", "="},
	"u<=":    {"swap", "u<", "0", "="},
	"u>=":    {"u<", "0", "="},
	"f>":     {"swap", "f<"},
	"negate": {"0", "swap", "-"},
	"invert": {"-1", "^"},
	"nip":    {"swap", "drop"},
	"over":   {"1", "pick"},
	"2drop":  {"drop", "drop"},
	"s>d":    {"dup", "<0"},
}

// peepholeRules are local rewrites, applied until nothing changes. Newline
// tokens are ignored while matching.
var peepholeRules = []struct {
	from, to []string
}{
	{[]string{"swap", "swap"}, nil},
	{[]string{"dup", "drop"}, nil},
	{[]string{"0", "+"}, nil},
	{[]string{"0", "-"}, nil},
	{[]string{"1", "*"}, nil},
	{[]string{"0", "=", "0", "=", "if"}, []string{"if"}},
	{[]string{"0", "=", "0", "=", "zgoto0"}, []string{"zgoto0"}},
}

func isLayout(t string) bool { return strings.HasPrefix(t, "\n") || strings.HasPrefix(t, "( ") }

func peephole(toks []string) []string {
	// inline expansion
	out := make([]string, 0, len(toks))
	for _, t := range toks {
		if rep, ok := inlineWords[t]; ok {
			out = append(out, rep...)
			continue
		}
		out = append(out, t)
	}
	for changed := true; changed; {
		changed = false
		// code token positions
		var idx []int
		for i, t := range out {
			if !isLayout(t) {
				idx = append(idx, i)
			}
		}
		for p := 0; p < len(idx) && !changed; p++ {
			for _, r := range peepholeRules {
				if p+len(r.from) > len(idx) {
					continue
				}
				match := true
				for k, w := range r.from {
					if out[idx[p+k]] != w {
						match = false
						break
					}
				}
				if !match {
					continue
				}
				// replace: put the new tokens at the first position, blank
				// the others (keeping layout tokens in between)
				first, lastI := idx[p], idx[p+len(r.from)-1]
				var repl []string
				repl = append(repl, r.to...)
				for i := first; i <= lastI; i++ {
					if isLayout(out[i]) {
						repl = append(repl, out[i])
					}
				}
				out = append(out[:first], append(repl, out[lastI+1:]...)...)
				changed = true
				break
			}
			if changed {
				break
			}
			// zgoto N zlabel N: a jump to the next instruction
			if p+3 < len(idx) && out[idx[p]] == "zgoto" && out[idx[p+2]] == "zlabel" && out[idx[p+1]] == out[idx[p+3]] {
				first := idx[p]
				out = append(out[:first], out[idx[p+2]:]...)
				changed = true
			}
			// k 2l@ swap drop -> k+1 l@ (a slice's length), k 2l@ drop -> k l@
			if !changed && p+3 < len(idx) && out[idx[p+1]] == "2l@" && isNumber(out[idx[p]]) && out[idx[p+2]] == "swap" && out[idx[p+3]] == "drop" {
				k, _ := strconv.Atoi(out[idx[p]])
				first, lastI := idx[p], idx[p+3]
				out = append(out[:first], append([]string{strconv.Itoa(k + 1), "l@"}, out[lastI+1:]...)...)
				changed = true
			}
			if !changed && p+2 < len(idx) && out[idx[p+1]] == "2l@" && isNumber(out[idx[p]]) && out[idx[p+2]] == "drop" {
				first, lastI := idx[p], idx[p+2]
				out = append(out[:first], append([]string{out[idx[p]], "l@"}, out[lastI+1:]...)...)
				changed = true
			}
			// k l! k l@ -> dup k l!
			if !changed && p+3 < len(idx) && out[idx[p+1]] == "l!" && out[idx[p+3]] == "l@" && out[idx[p]] == out[idx[p+2]] && isNumber(out[idx[p]]) {
				k := out[idx[p]]
				first, lastI := idx[p], idx[p+3]
				repl := []string{"dup", k, "l!"}
				out = append(out[:first], append(repl, out[lastI+1:]...)...)
				changed = true
			}
		}
	}
	return out
}

func isNumber(s string) bool {
	if s == "" {
		return false
	}
	for _, r := range s {
		if r < '0' || r > '9' {
			return false
		}
	}
	return true
}

// isStaticInit reports whether a package variable's initializer can run at
// load time (so it is baked into a ROM image): constants, and composite
// literals built only from constants.
func (c *compiler) isStaticInit(e ast.Expr) bool {
	tv := c.info.Types[e]
	if tv.Value != nil || tv.IsNil() {
		return true
	}
	switch x := e.(type) {
	case *ast.ParenExpr:
		return c.isStaticInit(x.X)
	case *ast.CompositeLit:
		for _, el := range x.Elts {
			if kv, ok := el.(*ast.KeyValueExpr); ok {
				el = kv.Value
			}
			if !c.isStaticInit(el) {
				return false
			}
		}
		return true
	case *ast.UnaryExpr:
		if x.Op == token.AND {
			if _, ok := x.X.(*ast.CompositeLit); ok {
				return c.isStaticInit(x.X)
			}
		}
	}
	return false
}

// loadTimeInit emits a static initializer as load-time code and reports
// whether it could.
func (c *compiler) loadTimeInit(g *progGen, v *types.Var, rhs ast.Expr) bool {
	if !c.isStaticInit(rhs) {
		return false
	}
	if v.Name() == "_" {
		return true
	}
	f := newFuncGen(c, g, nil)
	f.storeInit(v, rhs, false)
	var parts []string
	for _, t := range f.toks {
		if !isLayout(t) {
			parts = append(parts, t)
		}
	}
	if len(parts) > 0 {
		g.load = append(g.load, strings.Join(parts, " "))
	}
	return true
}

// storeInit stores an initializer into a variable, filling composite
// literals in place.
func (f *fgen) storeInit(v *types.Var, rhs ast.Expr, zero bool) {
	t := v.Type()
	if lit, ok := ast.Unparen(rhs).(*ast.CompositeLit); ok && kindOf(t).inMemory() {
		base := f.c.globals[v]
		if base == "" {
			base = f.c.statics[v]
		}
		f.fillLit(base, 0, t, lit, zero)
		return
	}
	f.expr(rhs)
	f.storeVar(v)
}
