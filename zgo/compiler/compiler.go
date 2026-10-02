// Package compiler translates ZGo, a strict subset of Go, into zForth source.
//
// The pipeline is: parse with go/parser, type-check with go/types for a
// 32-bit target, validate the subset (rejecting everything else with a
// targeted message), then generate Forth for each function, in call-graph
// order, plus a JSON manifest of the exported entry points.
package compiler

import (
	"fmt"
	"go/ast"
	"go/parser"
	"go/token"
	"go/types"
	"os"
	"sort"
	"strings"

	"zgo/dev"
)

// Options control code generation.
type Options struct {
	// NoBounds drops array, slice and string index checks.
	NoBounds bool
	// Comments adds a comment with the source position of each statement.
	Comments bool
	// NoOptimize turns off the peephole optimizer.
	NoOptimize bool
}

// Result is the output of a successful compilation.
type Result struct {
	Forth    string
	Manifest *Manifest
}

// Error is a compile error at a source position.
type Error struct {
	Pos token.Position
	Msg string
}

func (e *Error) Error() string {
	if e.Pos.IsValid() {
		return fmt.Sprintf("%s: %s", e.Pos, e.Msg)
	}
	return e.Msg
}

// ErrorList is the list of errors from a failed compilation, sorted by
// position.
type ErrorList []*Error

func (l ErrorList) Error() string {
	var b strings.Builder
	for i, e := range l {
		if i > 0 {
			b.WriteByte('\n')
		}
		b.WriteString(e.Error())
	}
	return b.String()
}

// CompileFiles reads and compiles the named .zgo files as one program.
func CompileFiles(filenames []string, opts Options) (*Result, error) {
	srcs := make([]Source, 0, len(filenames))
	for _, name := range filenames {
		data, err := os.ReadFile(name)
		if err != nil {
			return nil, err
		}
		srcs = append(srcs, Source{Name: name, Text: data})
	}
	return Compile(srcs, opts)
}

// Source is one input file.
type Source struct {
	Name string
	Text []byte
}

// Compile compiles the given sources as one program.
func Compile(srcs []Source, opts Options) (*Result, error) {
	c := &compiler{
		fset:     token.NewFileSet(),
		opts:     opts,
		funcs:    map[*types.Func]*funcInfo{},
		host:     map[*types.Func]string{},
		globals:  map[*types.Var]string{},
		statics:  map[*types.Var]string{},
		names:    map[string]bool{},
		strings:  map[string]string{},
		stubbed:  map[*funcInfo]bool{},
		inlineFn: map[*types.Func]string{},
	}
	if len(srcs) == 0 {
		return nil, ErrorList{{Msg: "no input files"}}
	}
	for _, s := range srcs {
		f, err := parser.ParseFile(c.fset, s.Name, s.Text, parser.ParseComments)
		if err != nil {
			c.addGoErrors(err)
			continue
		}
		c.files = append(c.files, f)
	}
	if len(c.errs) > 0 {
		return nil, c.errorList()
	}
	c.validateSyntax()
	if len(c.errs) > 0 {
		return nil, c.errorList()
	}
	c.typeCheck()
	if len(c.errs) > 0 {
		return nil, c.errorList()
	}
	c.validateTypes()
	if len(c.errs) > 0 {
		return nil, c.errorList()
	}
	out := c.generate()
	if len(c.errs) > 0 {
		return nil, c.errorList()
	}
	return &Result{Forth: out, Manifest: c.manifest()}, nil
}

type compiler struct {
	fset  *token.FileSet
	opts  Options
	files []*ast.File
	errs  ErrorList

	pkg    *types.Package
	devPkg *types.Package
	info   *types.Info

	funcs    map[*types.Func]*funcInfo
	order    []*funcInfo            // functions with bodies, in source order
	host     map[*types.Func]string // bodyless functions: the Forth they call
	inlineFn map[*types.Func]string // dev functions built into the compiler
	globals  map[*types.Var]string  // package variables: their Forth word
	statics  map[*types.Var]string  // local arrays and structs: static storage
	names    map[string]bool        // Forth names in use
	strings  map[string]string      // shared string literals: their word
	stubbed  map[*funcInfo]bool     // mutually recursive functions with stubs

	parents map[ast.Node]ast.Node // filled on first use by parentOf

	exports    []*funcInfo
	exportVars []*types.Var
	inits      []*funcInfo
	mainFn     *funcInfo
}

func (c *compiler) errorf(pos token.Pos, format string, args ...any) {
	c.errs = append(c.errs, &Error{Pos: c.fset.Position(pos), Msg: fmt.Sprintf(format, args...)})
}

func (c *compiler) addGoErrors(err error) {
	switch e := err.(type) {
	case interface{ Unwrap() []error }:
		for _, x := range e.Unwrap() {
			c.addGoErrors(x)
		}
	case types.Error:
		c.errs = append(c.errs, &Error{Pos: e.Fset.Position(e.Pos), Msg: e.Msg})
	default:
		// scanner.ErrorList and friends format as "pos: msg"
		for _, line := range strings.Split(err.Error(), "\n") {
			if line != "" {
				c.errs = append(c.errs, parseErrorLine(line))
			}
		}
	}
}

func parseErrorLine(line string) *Error {
	// file:line:col: msg
	parts := strings.SplitN(line, ": ", 2)
	if len(parts) == 2 {
		var p token.Position
		loc := strings.Split(parts[0], ":")
		if len(loc) >= 3 {
			p.Filename = strings.Join(loc[:len(loc)-2], ":")
			fmt.Sscanf(loc[len(loc)-2], "%d", &p.Line)
			fmt.Sscanf(loc[len(loc)-1], "%d", &p.Column)
			return &Error{Pos: p, Msg: parts[1]}
		}
	}
	return &Error{Msg: line}
}

func (c *compiler) errorList() ErrorList {
	sort.SliceStable(c.errs, func(i, j int) bool {
		a, b := c.errs[i].Pos, c.errs[j].Pos
		if a.Filename != b.Filename {
			return a.Filename < b.Filename
		}
		if a.Line != b.Line {
			return a.Line < b.Line
		}
		return a.Column < b.Column
	})
	// drop exact duplicates
	out := ErrorList{}
	for i, e := range c.errs {
		if i > 0 && e.Error() == c.errs[i-1].Error() {
			continue
		}
		out = append(out, e)
	}
	if len(out) > 20 {
		out = append(out[:20], &Error{Msg: "too many errors"})
	}
	return out
}

// sizes makes int, uint and uintptr 32-bit, as on the device.
var sizes = &types.StdSizes{WordSize: 4, MaxAlign: 4}

type importer struct{ c *compiler }

func (im importer) Import(path string) (*types.Package, error) {
	if path != "dev" {
		return nil, fmt.Errorf("package %q is not available", path)
	}
	if im.c.devPkg != nil {
		return im.c.devPkg, nil
	}
	f, err := parser.ParseFile(im.c.fset, "dev/dev.zgo", dev.Source, parser.ParseComments)
	if err != nil {
		return nil, err
	}
	conf := types.Config{Sizes: sizes, GoVersion: "go1.22"}
	info := &types.Info{Defs: map[*ast.Ident]types.Object{}}
	pkg, err := conf.Check("dev", im.c.fset, []*ast.File{f}, info)
	if err != nil {
		return nil, err
	}
	im.c.devPkg = pkg
	im.c.collectHost(f, info, true)
	return pkg, nil
}

func (c *compiler) typeCheck() {
	c.info = &types.Info{
		Types:      map[ast.Expr]types.TypeAndValue{},
		Defs:       map[*ast.Ident]types.Object{},
		Uses:       map[*ast.Ident]types.Object{},
		Implicits:  map[ast.Node]types.Object{},
		Selections: map[*ast.SelectorExpr]*types.Selection{},
		Scopes:     map[ast.Node]*types.Scope{},
	}
	conf := types.Config{
		Sizes:     sizes,
		GoVersion: "go1.22",
		Importer:  importer{c},
		Error: func(err error) {
			if te, ok := err.(types.Error); ok && te.Soft && strings.Contains(te.Msg, "declared and not used") {
				// Go rejects unused variables and imports too; keep that
				c.addGoErrors(err)
				return
			}
			c.addGoErrors(err)
		},
	}
	pkg, _ := conf.Check("main", c.fset, c.files, c.info)
	c.pkg = pkg
	if pkg != nil && pkg.Name() != "main" {
		c.errorf(c.files[0].Name.Pos(), "ZGo programs must be package main, not package %s", pkg.Name())
	}
}

// collectHost records bodyless functions bound with //zf:forth.
func (c *compiler) collectHost(f *ast.File, info *types.Info, isDev bool) {
	for _, d := range f.Decls {
		fd, ok := d.(*ast.FuncDecl)
		if !ok || fd.Body != nil {
			continue
		}
		obj, _ := info.Defs[fd.Name].(*types.Func)
		if obj == nil {
			continue
		}
		if word := forthDirective(fd.Doc); word != "" {
			c.host[obj] = word
		} else if isDev {
			c.inlineFn[obj] = fd.Name.Name
		} else {
			c.errorf(fd.Pos(), "function %s has no body: bind it to a zForth word with a //zf:forth comment", fd.Name.Name)
			c.host[obj] = "" // already reported; calls compile to nothing
		}
	}
}

// forthDirective returns the text of a //zf:forth comment. It is read from
// the raw comments: CommentGroup.Text drops directive-style comments.
func forthDirective(g *ast.CommentGroup) string {
	if g == nil {
		return ""
	}
	for _, cm := range g.List {
		if rest, ok := strings.CutPrefix(cm.Text, "//zf:forth"); ok {
			if rest == "" || rest[0] == ' ' || rest[0] == '\t' {
				return strings.TrimSpace(rest)
			}
		}
	}
	return ""
}
