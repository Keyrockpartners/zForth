// Command zgoc compiles ZGo programs to zForth.
//
//	zgoc build [-o out.zf] [-manifest out.json] file.zgo...
//	zgoc run   [-host host.zf] [-calls calls.txt] [-i] file.zgo...
//	zgoc image -o dir [-san none|asan|ubsan] file.zgo...
//	zgoc test  [-san ...] [-update] dir...
//
// run, image and test need the zForth tree (forth/bs.zf and src/): zgoc
// looks for it upward from the current directory, or in $ZFORTH_ROOT.
package main

import (
	"bytes"
	"errors"
	"flag"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"sort"
	"strconv"
	"strings"

	"zgo/compiler"
)

func usage() {
	fmt.Fprint(os.Stderr, `usage:
  zgoc build [-o out.zf] [-manifest out.json] [flags] file.zgo...
        compile to zForth source and a JSON manifest of exported functions
  zgoc run [-host host.zf] [-calls calls.txt] [-i] [flags] file.zgo...
        compile and run on the Linux zforth: z-init, z-main, then calls
  zgoc image -o dir [-san none|asan|ubsan] [flags] file.zgo...
        build a standalone Linux binary with the program in a ROM image
  zgoc test [-san none|asan|ubsan] [-update] dir...
        run example directories (main.zgo + expected.txt) in run and image
        mode, and error-test directories (files with // ERROR comments)

compile flags: -nobounds (drop index checks), -comments (source positions
in the output), -O0 (no peephole optimization)
host.zf and calls.txt next to the first source file are used by default.
`)
	os.Exit(2)
}

func main() {
	if len(os.Args) < 2 {
		usage()
	}
	cmd, args := os.Args[1], os.Args[2:]
	var err error
	switch cmd {
	case "build":
		err = cmdBuild(args)
	case "run":
		err = cmdRun(args)
	case "image":
		err = cmdImage(args)
	case "test":
		err = cmdTest(args)
	case "help", "-h", "--help":
		usage()
	default:
		fmt.Fprintf(os.Stderr, "zgoc: unknown command %q\n", cmd)
		usage()
	}
	if err != nil {
		var el compiler.ErrorList
		if errors.As(err, &el) {
			for _, e := range el {
				fmt.Fprintln(os.Stderr, e)
			}
		} else {
			fmt.Fprintln(os.Stderr, "zgoc:", err)
		}
		os.Exit(1)
	}
}

type compileFlags struct {
	nobounds, comments, o0 bool
}

func (c *compileFlags) register(fs *flag.FlagSet) {
	fs.BoolVar(&c.nobounds, "nobounds", false, "drop array, slice and string index checks")
	fs.BoolVar(&c.comments, "comments", false, "add source positions to the output")
	fs.BoolVar(&c.o0, "O0", false, "turn off the peephole optimizer")
}

func (c *compileFlags) options() compiler.Options {
	return compiler.Options{NoBounds: c.nobounds, Comments: c.comments, NoOptimize: c.o0}
}

func parse(fs *flag.FlagSet, args []string) []string {
	fs.Usage = usage
	_ = fs.Parse(args)
	if fs.NArg() == 0 {
		usage()
	}
	return fs.Args()
}

func cmdBuild(args []string) error {
	fs := flag.NewFlagSet("build", flag.ExitOnError)
	out := fs.String("o", "", "output file (default: first source with .zf)")
	man := fs.String("manifest", "", "manifest file (default: output with .json)")
	var cf compileFlags
	cf.register(fs)
	files := parse(fs, args)
	if *out == "" {
		*out = strings.TrimSuffix(files[0], ".zgo") + ".zf"
	}
	if *man == "" {
		*man = strings.TrimSuffix(*out, ".zf") + ".json"
	}
	_, err := build(files, *out, *man, cf.options())
	return err
}

func build(files []string, out, man string, opts compiler.Options) (*compiler.Result, error) {
	res, err := compiler.CompileFiles(files, opts)
	if err != nil {
		return nil, err
	}
	if err := os.WriteFile(out, []byte(res.Forth), 0o644); err != nil {
		return nil, err
	}
	if man != "" {
		if err := os.WriteFile(man, res.Manifest.JSON(), 0o644); err != nil {
			return nil, err
		}
	}
	return res, nil
}

// root finds the zForth tree.
func root() (string, error) {
	if r := os.Getenv("ZFORTH_ROOT"); r != "" {
		return filepath.Abs(r)
	}
	dir, _ := os.Getwd()
	for {
		if _, err := os.Stat(filepath.Join(dir, "forth", "bs.zf")); err == nil {
			return dir, nil
		}
		parent := filepath.Dir(dir)
		if parent == dir {
			return "", errors.New("cannot find the zForth tree (forth/bs.zf): run inside it or set ZFORTH_ROOT")
		}
		dir = parent
	}
}

// companion returns path if set, else name next to the first source file
// if it exists.
func companion(path string, files []string, name string) string {
	if path != "" {
		return path
	}
	p := filepath.Join(filepath.Dir(files[0]), name)
	if _, err := os.Stat(p); err == nil {
		return p
	}
	return ""
}

func zforthBin(r string) (string, error) {
	bin := filepath.Join(r, "src", "linux", "zforth")
	if _, err := os.Stat(bin); err != nil {
		return "", fmt.Errorf("%s not found: run make first", bin)
	}
	return bin, nil
}

func absAll(paths ...string) []string {
	var out []string
	for _, p := range paths {
		if p == "" {
			continue
		}
		a, err := filepath.Abs(p)
		if err != nil {
			a = p
		}
		out = append(out, a)
	}
	return out
}

// runArgs is the zforth command line after the sources: z-init, z-main,
// then the calls file. zforth runs in the zForth tree (bs.zf includes its
// files by relative path), and the calls file is named relative to it so
// error messages are the same on every machine.
func runArgs(r, calls string) []string {
	args := []string{"-e", "z-init", "-e", "z-main"}
	if calls != "" {
		args = append(args, relTo(r, calls))
	}
	return args
}

func relTo(r, p string) string {
	a, err := filepath.Abs(p)
	if err != nil {
		return p
	}
	if rel, err := filepath.Rel(r, a); err == nil && !strings.HasPrefix(rel, "..") {
		return rel
	}
	return a
}

func cmdRun(args []string) error {
	fs := flag.NewFlagSet("run", flag.ExitOnError)
	host := fs.String("host", "", "Forth file with host word stand-ins (default: host.zf next to the source)")
	calls := fs.String("calls", "", "Forth lines to run after main (default: calls.txt next to the source)")
	inter := fs.Bool("i", false, "stay in the REPL afterwards")
	var cf compileFlags
	cf.register(fs)
	files := parse(fs, args)
	r, err := root()
	if err != nil {
		return err
	}
	bin, err := zforthBin(r)
	if err != nil {
		return err
	}
	tmp, err := os.MkdirTemp("", "zgoc")
	if err != nil {
		return err
	}
	defer os.RemoveAll(tmp)
	out := filepath.Join(tmp, "prog.zf")
	if _, err := build(files, out, "", cf.options()); err != nil {
		return err
	}
	h := companion(*host, files, "host.zf")
	cl := companion(*calls, files, "calls.txt")
	cmd := runCommand(r, bin, h, out, cl, !*inter)
	cmd.Stdin, cmd.Stdout, cmd.Stderr = os.Stdin, os.Stdout, os.Stderr
	if err := cmd.Run(); err != nil {
		var ee *exec.ExitError
		if errors.As(err, &ee) {
			os.Exit(ee.ExitCode())
		}
		return err
	}
	return nil
}

func runCommand(r, bin, host, prog, calls string, exit bool) *exec.Cmd {
	args := []string{"-q"}
	if exit {
		args = append(args, "-x")
	}
	args = append(args, absAll(filepath.Join(r, "forth", "bs.zf"), filepath.Join(r, "forth", "zgo.zf"), host, prog)...)
	args = append(args, runArgs(r, calls)...)
	cmd := exec.Command(bin, args...)
	cmd.Dir = r
	return cmd
}

func cmdImage(args []string) error {
	fs := flag.NewFlagSet("image", flag.ExitOnError)
	out := fs.String("o", "", "output directory (required)")
	san := fs.String("san", "none", "sanitizers: none, asan or ubsan")
	host := fs.String("host", "", "Forth file with host word stand-ins (default: host.zf next to the source)")
	var cf compileFlags
	cf.register(fs)
	files := parse(fs, args)
	if *out == "" {
		usage()
	}
	_, err := image(files, *out, *san, *host, cf.options())
	return err
}

// image builds a standalone binary with the program in a ROM image and
// returns its path.
func image(files []string, dir, san, host string, opts compiler.Options) (string, error) {
	r, err := root()
	if err != nil {
		return "", err
	}
	bin, err := zforthBin(r)
	if err != nil {
		return "", err
	}
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return "", err
	}
	name := filepath.Base(dir)
	prog := filepath.Join(dir, name+".zf")
	if _, err := build(files, prog, filepath.Join(dir, name+".json"), opts); err != nil {
		return "", err
	}
	// 1. export the dictionary with bs.zf, zgo.zf, the host stand-ins and
	// the program as a C header
	header := filepath.Join(dir, "image.h")
	h := companion(host, files, "host.zf")
	exp := exec.Command(bin, append([]string{"-H", "zforth_dict"},
		absAll(filepath.Join(r, "forth", "bs.zf"), filepath.Join(r, "forth", "zgo.zf"), h, prog)...)...)
	var hdr, errb bytes.Buffer
	exp.Stdout, exp.Stderr = &hdr, &errb
	if err := exp.Run(); err != nil || errb.Len() > 0 {
		return "", fmt.Errorf("exporting the ROM image failed: %v\n%s", err, errb.String())
	}
	if err := os.WriteFile(header, hdr.Bytes(), 0o644); err != nil {
		return "", err
	}
	// 2. compile the Linux host with the image as its ROM dictionary
	cc := os.Getenv("CC")
	if cc == "" {
		cc = "cc"
	}
	absHeader, _ := filepath.Abs(header)
	outBin := filepath.Join(dir, name)
	ccArgs := []string{"-I" + filepath.Join(r, "src", "linux"), "-I" + filepath.Join(r, "src", "zforth"),
		"-g", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-Wno-unused-result",
		"-DZF_ENABLE_ROM_DICT=1", "-DZF_LINUX_ROM_DICT=1", "-DZF_DICT_HEADER=\"" + absHeader + "\""}
	switch san {
	case "none", "":
		ccArgs = append(ccArgs, "-Os")
	case "asan":
		ccArgs = append(ccArgs, "-Os", "-fsanitize=address")
	case "ubsan":
		ccArgs = append(ccArgs, "-O1", "-fsanitize=address,undefined", "-fno-sanitize-recover=all")
	default:
		return "", fmt.Errorf("unknown -san %q", san)
	}
	ccArgs = append(ccArgs, "-o", outBin, filepath.Join(r, "src", "linux", "main.c"), filepath.Join(r, "src", "zforth", "zforth.c"), "-lm")
	c := exec.Command(cc, ccArgs...)
	c.Stdout, c.Stderr = os.Stdout, os.Stderr
	if err := c.Run(); err != nil {
		return "", fmt.Errorf("compiling %s: %v", outBin, err)
	}
	return outBin, nil
}

func cmdTest(args []string) error {
	fs := flag.NewFlagSet("test", flag.ExitOnError)
	san := fs.String("san", "none", "sanitizers for the image binaries: none, asan or ubsan")
	update := fs.Bool("update", false, "rewrite expected.txt and the committed main.zf/main.json")
	build := fs.String("build", "", "directory for image binaries (default: build/zgo-test in the zForth tree)")
	dirs := parse(fs, args)
	r, err := root()
	if err != nil {
		return err
	}
	if *build == "" {
		*build = filepath.Join(r, "build", "zgo-test")
	}
	pass, fail := 0, 0
	for _, d := range dirs {
		if st, err := os.Stat(d); err == nil && !st.IsDir() {
			continue // e.g. a README next to the example directories
		}
		var errs []string
		if _, err := os.Stat(filepath.Join(d, "expected.txt")); err == nil || fileExists(filepath.Join(d, "main.zgo")) {
			errs = testExample(r, d, *build, *san, *update)
		} else {
			errs = testErrors(d)
		}
		if len(errs) == 0 {
			pass++
			fmt.Printf("ok    %s\n", d)
		} else {
			fail++
			fmt.Printf("FAIL  %s\n", d)
			for _, e := range errs {
				fmt.Printf("      %s\n", strings.ReplaceAll(e, "\n", "\n      "))
			}
		}
	}
	fmt.Printf("zgo: %d passed, %d failed\n", pass, fail)
	if fail > 0 {
		return errors.New("tests failed")
	}
	return nil
}

func fileExists(p string) bool {
	_, err := os.Stat(p)
	return err == nil
}

func zgoFiles(dir string) []string {
	m, _ := filepath.Glob(filepath.Join(dir, "*.zgo"))
	sort.Strings(m)
	return m
}

// testExample runs an example in run mode and image mode and compares the
// output with expected.txt; the committed main.zf and main.json must match
// the compiler's output.
func testExample(r, dir, buildDir, san string, update bool) []string {
	var errs []string
	files := zgoFiles(dir)
	if len(files) == 0 {
		return []string{"no .zgo files"}
	}
	res, err := compiler.CompileFiles(files, compiler.Options{})
	if err != nil {
		return []string{err.Error()}
	}
	// committed outputs
	for _, g := range []struct{ name, text string }{
		{"main.zf", res.Forth}, {"main.json", string(res.Manifest.JSON())},
	} {
		p := filepath.Join(dir, g.name)
		old, err := os.ReadFile(p)
		if update {
			if err != nil || string(old) != g.text {
				os.WriteFile(p, []byte(g.text), 0o644)
			}
			continue
		}
		if err != nil {
			errs = append(errs, fmt.Sprintf("%s is missing: run zgoc test -update", g.name))
		} else if string(old) != g.text {
			errs = append(errs, fmt.Sprintf("%s is out of date: run zgoc test -update", g.name))
		}
	}
	bin, err := zforthBin(r)
	if err != nil {
		return append(errs, err.Error())
	}
	calls := companion("", files, "calls.txt")
	host := companion("", files, "host.zf")
	// run mode, on the committed (or just written) main.zf
	cmd := runCommand(r, bin, host, filepath.Join(dir, "main.zf"), calls, true)
	runOut, _ := cmd.CombinedOutput()
	// image mode
	name := filepath.Base(filepath.Clean(dir))
	imgDir := filepath.Join(buildDir, name)
	imgBin, err := image(files, imgDir, san, host, compiler.Options{})
	var imgOut []byte
	if err != nil {
		errs = append(errs, "image: "+err.Error())
	} else {
		absBin, _ := filepath.Abs(imgBin)
		c := exec.Command(absBin, append([]string{"-q", "-x"}, runArgs(r, calls)...)...)
		c.Dir = r
		imgOut, _ = c.CombinedOutput()
	}
	expPath := filepath.Join(dir, "expected.txt")
	if update {
		os.WriteFile(expPath, runOut, 0o644)
	}
	want, err := os.ReadFile(expPath)
	if err != nil {
		return append(errs, "expected.txt is missing")
	}
	if !bytes.Equal(runOut, want) {
		errs = append(errs, "run mode output differs from expected.txt:\n"+diff(string(want), string(runOut)))
	}
	if imgOut != nil && !bytes.Equal(imgOut, want) {
		errs = append(errs, "image mode output differs from expected.txt:\n"+diff(string(want), string(imgOut)))
	}
	return errs
}

// diff shows the first differing line.
func diff(want, got string) string {
	wl, gl := strings.Split(want, "\n"), strings.Split(got, "\n")
	for i := 0; i < max(len(wl), len(gl)); i++ {
		var w, g string
		if i < len(wl) {
			w = wl[i]
		}
		if i < len(gl) {
			g = gl[i]
		}
		if w != g {
			return fmt.Sprintf("line %d:\n  want: %q\n  got:  %q", i+1, w, g)
		}
	}
	return "(trailing difference)"
}

var errorComment = regexp.MustCompile(`// ERROR ("(?:[^"\\]|\\.)*")`)

// testErrors compiles each .zgo file in dir on its own; every line with
// an // ERROR "text" comment must get an error containing text, and no
// other line may get one.
func testErrors(dir string) []string {
	var errs []string
	files := zgoFiles(dir)
	if len(files) == 0 {
		return []string{"no .zgo files"}
	}
	for _, file := range files {
		src, err := os.ReadFile(file)
		if err != nil {
			errs = append(errs, err.Error())
			continue
		}
		want := map[int]string{}
		for i, line := range strings.Split(string(src), "\n") {
			if m := errorComment.FindStringSubmatch(line); m != nil {
				text, err := strconv.Unquote(m[1])
				if err != nil {
					text = m[1]
				}
				want[i+1] = text
			}
		}
		if len(want) == 0 {
			errs = append(errs, file+": no // ERROR comments")
			continue
		}
		_, cerr := compiler.CompileFiles([]string{file}, compiler.Options{})
		var el compiler.ErrorList
		if !errors.As(cerr, &el) {
			errs = append(errs, fmt.Sprintf("%s: expected errors, compiled without any (%v)", file, cerr))
			continue
		}
		got := map[int][]string{}
		for _, e := range el {
			got[e.Pos.Line] = append(got[e.Pos.Line], e.Msg)
		}
		for line, text := range want {
			found := false
			for _, m := range got[line] {
				if strings.Contains(m, text) {
					found = true
				}
			}
			if !found {
				errs = append(errs, fmt.Sprintf("%s:%d: want an error containing %q, got %q", file, line, text, got[line]))
			}
		}
		for line, msgs := range got {
			if _, ok := want[line]; !ok {
				errs = append(errs, fmt.Sprintf("%s:%d: unexpected error: %s", file, line, strings.Join(msgs, "; ")))
			}
		}
	}
	sort.Strings(errs)
	return errs
}
