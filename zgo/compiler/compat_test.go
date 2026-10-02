package compiler

import (
	"bytes"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
)

// TestCompat runs each program in testdata/compat with ZGo (on the Linux
// zforth) and with the real Go toolchain, and requires the same output.
// It is skipped if src/linux/zforth has not been built. Go's int is 64-bit
// on the build machine, so these programs avoid int overflow.
func TestCompat(t *testing.T) {
	root := repoRoot(t)
	zf := filepath.Join(root, "src", "linux", "zforth")
	if _, err := os.Stat(zf); err != nil {
		t.Skip("src/linux/zforth not built (run make)")
	}
	goBin, err := exec.LookPath("go")
	if err != nil {
		t.Skip("no go toolchain")
	}
	files, _ := filepath.Glob(filepath.Join("..", "testdata", "compat", "*.zgo"))
	if len(files) == 0 {
		t.Fatal("no compat programs")
	}
	// the examples that need no host words or firmware calls, except
	// strings, which shows the string(buf[:n]) view difference on purpose
	examples, _ := filepath.Glob(filepath.Join(root, "examples", "*", "main.zgo"))
	for _, ex := range examples {
		dir := filepath.Dir(ex)
		if filepath.Base(dir) == "strings" || exists(filepath.Join(dir, "host.zf")) || exists(filepath.Join(dir, "calls.txt")) {
			continue
		}
		files = append(files, ex)
	}
	for _, file := range files {
		file := file
		name := strings.TrimSuffix(filepath.Base(file), ".zgo")
		if name == "main" {
			name = "example-" + filepath.Base(filepath.Dir(file))
		}
		t.Run(name, func(t *testing.T) {
			t.Parallel()
			src, err := os.ReadFile(file)
			if err != nil {
				t.Fatal(err)
			}
			want := runGo(t, goBin, src)
			for _, opts := range []Options{{}, {NoOptimize: true}} {
				got := runZGo(t, root, zf, file, src, opts)
				if got != want {
					t.Errorf("output differs from Go (options %+v):\n%s", opts, firstDiff(want, got))
				}
			}
		})
	}
}

func exists(p string) bool {
	_, err := os.Stat(p)
	return err == nil
}

func repoRoot(t *testing.T) string {
	dir, _ := os.Getwd()
	for {
		if _, err := os.Stat(filepath.Join(dir, "forth", "bs.zf")); err == nil {
			return dir
		}
		parent := filepath.Dir(dir)
		if parent == dir {
			t.Fatal("cannot find the zForth tree")
		}
		dir = parent
	}
}

func runZGo(t *testing.T, root, zf, name string, src []byte, opts Options) string {
	res, err := Compile([]Source{{Name: name, Text: src}}, opts)
	if err != nil {
		t.Fatalf("compile: %v", err)
	}
	prog := filepath.Join(t.TempDir(), "prog.zf")
	if err := os.WriteFile(prog, []byte(res.Forth), 0o644); err != nil {
		t.Fatal(err)
	}
	cmd := exec.Command(zf, "-q", "-x", "forth/bs.zf", "forth/zgo.zf", prog, "-e", "z-init", "-e", "z-main")
	cmd.Dir = root
	out, _ := cmd.CombinedOutput()
	return string(out)
}

const devStandIn = `package dev

import "fmt"

func Printf(format string, args ...any) { fmt.Printf(format, args...) }
func Format(buf []byte, format string, args ...any) int {
	return copy(buf, fmt.Sprintf(format, args...))
}
func Print(s string)  { fmt.Print(s) }
func Delay(ms uint32) {}
func Millis() uint64  { return 0 }
`

func runGo(t *testing.T, goBin string, src []byte) string {
	dir := t.TempDir()
	must := func(err error) {
		if err != nil {
			t.Fatal(err)
		}
	}
	must(os.MkdirAll(filepath.Join(dir, "dev"), 0o755))
	must(os.WriteFile(filepath.Join(dir, "go.mod"), []byte("module ex\n\ngo 1.22\n"), 0o644))
	must(os.WriteFile(filepath.Join(dir, "dev", "dev.go"), []byte(devStandIn), 0o644))
	text := strings.Replace(string(src), `"dev"`, `"ex/dev"`, 1)
	must(os.WriteFile(filepath.Join(dir, "main.go"), []byte(text), 0o644))
	cmd := exec.Command(goBin, "run", ".")
	cmd.Dir = dir
	var out bytes.Buffer
	cmd.Stdout = &out
	cmd.Stderr = &out
	if err := cmd.Run(); err != nil {
		t.Fatalf("go run: %v\n%s", err, out.String())
	}
	return out.String()
}

func firstDiff(want, got string) string {
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
			return fmt.Sprintf("line %d:\n  go:  %q\n  zgo: %q", i+1, w, g)
		}
	}
	return "(no difference)"
}
