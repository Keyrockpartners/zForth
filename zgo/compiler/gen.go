package compiler

import (
	"fmt"
	"go/ast"
	"go/token"
	"go/types"
	"hash/fnv"
	"sort"
	"strings"
)

// funcInfo is a function with a body (or a method).
type funcInfo struct {
	decl      *ast.FuncDecl
	obj       *types.Func
	word      string // Forth word name
	callees   []*funcInfo
	recursive bool   // part of a call cycle, including calling itself
	stubXT    string // call vector word, for mutually recursive functions
	index     int    // Tarjan bookkeeping
	low       int
	onStack   bool
	visited   bool
}

// Naming (ZGO_PLAN.md 5.1): exported functions are z-Name, init is z-init
// (calling every init function), main is z-main. Everything else from the
// program is z.name, methods z.Type.name. Words the compiler makes up use
// z: so they can never clash with a Go identifier.
const maxWord = 31

func (c *compiler) mangle(name string) string {
	return c.unique(shorten("z." + name))
}

func shorten(w string) string {
	if len(w) <= maxWord {
		return w
	}
	h := fnv.New32a()
	h.Write([]byte(w))
	return fmt.Sprintf("%s~%08x", w[:maxWord-9], h.Sum32())
}

// unique returns w, or w with a numeric suffix if w is taken.
func (c *compiler) unique(w string) string {
	if !c.names[w] {
		c.names[w] = true
		return w
	}
	for i := 2; ; i++ {
		s := fmt.Sprintf(".%d", i)
		cand := w + s
		if len(cand) > maxWord {
			cand = w[:maxWord-len(s)] + s
		}
		if !c.names[cand] {
			c.names[cand] = true
			return cand
		}
	}
}

// generate produces the Forth program.
func (c *compiler) generate() string {
	c.collectFuncs()
	c.callGraph()
	g := &progGen{c: c}
	return g.run()
}

func (c *compiler) collectFuncs() {
	for _, f := range c.files {
		c.collectHost(f, c.info, false)
		for _, d := range f.Decls {
			fd, ok := d.(*ast.FuncDecl)
			if !ok || fd.Body == nil {
				continue
			}
			obj := c.info.Defs[fd.Name].(*types.Func)
			fi := &funcInfo{decl: fd, obj: obj}
			name := fd.Name.Name
			switch {
			case fd.Recv != nil:
				rt := c.typeOf(fd.Recv.List[0].Type)
				if p, ok := rt.(*types.Pointer); ok {
					rt = p.Elem()
				}
				tn := rt.(*types.Named).Obj().Name()
				fi.word = c.mangle(tn + "." + name)
				c.checkReceiver(fd)
			case name == "init":
				fi.word = c.unique("z:init")
				c.inits = append(c.inits, fi)
			case name == "main":
				fi.word = "z-main"
				c.names[fi.word] = true
				c.mainFn = fi
			case obj.Exported():
				fi.word = "z-" + name
				if len(fi.word) > maxWord {
					c.errorf(fd.Name.Pos(), "exported name %s is too long: zForth words are at most 31 bytes, so exported names can have at most 29", name)
				}
				c.names[fi.word] = true
				c.exports = append(c.exports, fi)
			default:
				fi.word = c.mangle(name)
			}
			c.funcs[obj] = fi
			c.order = append(c.order, fi)
		}
	}
}

// checkReceiver allows value receivers only when the method treats the
// receiver as read-only, since it is passed by address.
func (c *compiler) checkReceiver(fd *ast.FuncDecl) {
	recv := fd.Recv.List[0]
	if _, isPtr := recv.Type.(*ast.StarExpr); isPtr || len(recv.Names) == 0 {
		return
	}
	obj := c.info.Defs[recv.Names[0]]
	if obj == nil {
		return
	}
	if pos, bad := c.modifies(fd.Body, obj.(*types.Var)); bad {
		c.errorf(pos, "a value receiver is read-only in ZGo (it is passed by address): use a pointer receiver, func (%s *%s)", recv.Names[0].Name, typeName(obj.Type()))
	}
}

// modifies reports whether body assigns to v (or a part of it) or takes
// its address.
func (c *compiler) modifies(body ast.Node, v *types.Var) (token.Pos, bool) {
	var pos token.Pos
	found := false
	isV := func(e ast.Expr) bool {
		for {
			switch x := e.(type) {
			case *ast.Ident:
				return c.info.Uses[x] == v
			case *ast.SelectorExpr:
				if kindOf(c.typeOf(x.X)) == kPointer {
					return false
				}
				e = x.X
			case *ast.IndexExpr:
				if kindOf(c.typeOf(x.X)) != kArray {
					return false
				}
				e = x.X
			case *ast.ParenExpr:
				e = x.X
			default:
				return false
			}
		}
	}
	ast.Inspect(body, func(n ast.Node) bool {
		if found {
			return false
		}
		switch n := n.(type) {
		case *ast.AssignStmt:
			for _, l := range n.Lhs {
				if isV(l) {
					pos, found = l.Pos(), true
				}
			}
		case *ast.IncDecStmt:
			if isV(n.X) {
				pos, found = n.X.Pos(), true
			}
		case *ast.UnaryExpr:
			if n.Op == token.AND && isV(n.X) {
				pos, found = n.Pos(), true
			}
		case *ast.SliceExpr:
			if kindOf(c.typeOf(n.X)) == kArray && isV(n.X) {
				pos, found = n.Pos(), true
			}
		case *ast.SelectorExpr:
			// a pointer-receiver method called on v (or a part of it)
			// takes its address implicitly
			if sel := c.info.Selections[n]; sel != nil && sel.Kind() == types.MethodVal {
				recv := sel.Obj().Type().(*types.Signature).Recv()
				if kindOf(recv.Type()) == kPointer && kindOf(c.typeOf(n.X)) != kPointer && isV(n.X) {
					pos, found = n.Pos(), true
				}
			}
		case *ast.RangeStmt:
			if n.Tok == token.ASSIGN {
				for _, e := range []ast.Expr{n.Key, n.Value} {
					if e != nil && isV(e) {
						pos, found = e.Pos(), true
					}
				}
			}
		}
		return true
	})
	return pos, found
}

// callGraph finds each function's callees and the strongly connected
// components (Tarjan), which give the emission order: callees first.
func (c *compiler) callGraph() {
	for _, fi := range c.order {
		seen := map[*funcInfo]bool{}
		ast.Inspect(fi.decl.Body, func(n ast.Node) bool {
			var id *ast.Ident
			switch n := n.(type) {
			case *ast.Ident:
				id = n
			default:
				return true
			}
			if obj, ok := c.info.Uses[id].(*types.Func); ok {
				if callee := c.funcs[obj]; callee != nil && !seen[callee] {
					seen[callee] = true
					fi.callees = append(fi.callees, callee)
				}
			}
			return true
		})
	}
}

// sccs returns the strongly connected components with callees first.
func (c *compiler) sccs() [][]*funcInfo {
	var out [][]*funcInfo
	var stack []*funcInfo
	index := 0
	var visit func(v *funcInfo)
	visit = func(v *funcInfo) {
		v.visited = true
		v.index, v.low = index, index
		index++
		stack = append(stack, v)
		v.onStack = true
		for _, w := range v.callees {
			if !w.visited {
				visit(w)
				v.low = min(v.low, w.low)
			} else if w.onStack {
				v.low = min(v.low, w.index)
			}
		}
		if v.low == v.index {
			var comp []*funcInfo
			for {
				w := stack[len(stack)-1]
				stack = stack[:len(stack)-1]
				w.onStack = false
				comp = append(comp, w)
				if w == v {
					break
				}
			}
			// keep source order within a component
			sort.SliceStable(comp, func(i, j int) bool { return comp[i].decl.Pos() < comp[j].decl.Pos() })
			out = append(out, comp)
		}
	}
	for _, fi := range c.order {
		if !fi.visited {
			visit(fi)
		}
	}
	for _, comp := range out {
		if len(comp) > 1 {
			for _, fi := range comp {
				fi.recursive = true
			}
		} else {
			for _, w := range comp[0].callees {
				if w == comp[0] {
					comp[0].recursive = true
				}
			}
		}
	}
	return out
}

// progGen assembles the program: the static data block, load-time
// initialization, functions and the entry words.
type progGen struct {
	c    *compiler
	out  strings.Builder
	data []dataItem // static storage, in order
	size int        // data block size so far
	load []string   // load-time initialization code lines
}

type dataItem struct {
	name   string
	offset int
	size   int
	what   string // comment
}

// alloc reserves size bytes in the static data block and returns the word
// that will push the address.
func (g *progGen) alloc(name string, size, align int, what string) string {
	if align < 1 {
		align = 1
	}
	g.size = (g.size + align - 1) / align * align
	g.data = append(g.data, dataItem{name: name, offset: g.size, size: size, what: what})
	g.size += size
	return name
}

func (g *progGen) run() string {
	c := g.c
	files := []string{}
	for _, f := range c.files {
		files = append(files, baseName(c.fset.Position(f.Pos()).Filename))
	}
	fmt.Fprintf(&g.out, "( Generated by zgoc from %s. Do not edit. )\n", commentSafe(strings.Join(files, " ")))
	fmt.Fprintf(&g.out, "( Needs forth/bs.zf and forth/zgo.zf. Run z-init after loading. )\n")

	// package variables, in source order
	var globals []*types.Var
	for _, f := range c.files {
		for _, d := range f.Decls {
			gd, ok := d.(*ast.GenDecl)
			if !ok || gd.Tok != token.VAR {
				continue
			}
			for _, sp := range gd.Specs {
				for _, id := range sp.(*ast.ValueSpec).Names {
					v, _ := c.info.Defs[id].(*types.Var)
					if v == nil {
						continue
					}
					globals = append(globals, v)
					if id.Name == "_" {
						continue
					}
					var name string
					if v.Exported() {
						// exported variables are public: the word pushes the
						// address, and the manifest lists it
						name = "z-" + id.Name
						if len(name) > maxWord {
							c.errorf(id.Pos(), "exported name %s is too long: zForth words are at most 31 bytes, so exported names can have at most 29", id.Name)
						}
						c.names[name] = true
						c.exportVars = append(c.exportVars, v)
					} else {
						name = c.mangle(id.Name)
					}
					c.globals[v] = g.alloc(name, sizeof(v.Type()), alignof(v.Type()), "var "+id.Name+" "+typeName(v.Type()))
				}
			}
		}
	}

	// functions: generate bodies first, since they allocate static storage
	comps := c.sccs()
	var bodies strings.Builder
	for _, comp := range comps {
		if len(comp) > 1 {
			for _, fi := range comp {
				c.stubbed[fi] = true
				xt := g.alloc(c.unique(shorten("z:xt."+strings.TrimPrefix(fi.word, "z."))), 4, 4, "call vector for "+fi.word)
				fmt.Fprintf(&bodies, ": %s %s @c zexec ;\n", fi.word, xt)
				fi.stubXT = xt
			}
		}
		for _, fi := range comp {
			f := newFuncGen(c, g, fi)
			bodies.WriteString(f.function())
			if fi.stubXT != "" {
				fmt.Fprintf(&bodies, "' %s %s !c\n", fi.word, fi.stubXT)
			}
		}
	}

	// z-init: dynamic package initializers in dependency order, then the
	// init functions in source order
	initGen := newFuncGen(c, g, nil)
	initBody := initGen.initFunction(globals)
	if c.mainFn == nil {
		initBody += ": z-main ;\n"
	}

	// data block and load-time initialization
	if g.size > 0 {
		fmt.Fprintf(&g.out, "%d buffer: z:data\n", g.size)
		for _, d := range g.data {
			fmt.Fprintf(&g.out, "z:data %d + constant %s ( %s )\n", d.offset, d.name, commentSafe(d.what))
		}
		fmt.Fprintf(&g.out, "z:data %d 0 fill\n", g.size)
	}
	for _, l := range g.load {
		g.out.WriteString(l)
		g.out.WriteByte('\n')
	}
	g.out.WriteString(bodies.String())
	g.out.WriteString(initBody)
	return g.out.String()
}

// commentSafe makes text safe inside a Forth ( comment ).
func commentSafe(s string) string {
	return strings.Map(func(r rune) rune {
		switch r {
		case '(':
			return '['
		case ')':
			return ']'
		case '\n':
			return ' '
		}
		return r
	}, s)
}
