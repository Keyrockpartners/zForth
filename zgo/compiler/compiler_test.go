package compiler

import (
	"go/constant"
	"go/token"
	"go/types"
	"strings"
	"testing"
)

func TestConstText(t *testing.T) {
	tests := []struct {
		v    constant.Value
		t    types.BasicKind
		want string
	}{
		{constant.MakeInt64(42), types.Int, "42"},
		{constant.MakeInt64(-1), types.Int32, "-1"},
		{constant.MakeUint64(4294967295), types.Uint32, "4294967295"},
		{constant.MakeInt64(200), types.Uint8, "200"},
		{constant.MakeInt64(-128), types.Int8, "-128"},
		{constant.MakeInt64(1700000000000), types.Uint64, "1700000000000."},
		{constant.MakeInt64(-5), types.Int64, "-5."},
		{constant.MakeUint64(18446744073709551615), types.Uint64, "18446744073709551615."},
		{constant.MakeFloat64(1.5), types.Float32, "1.5e+00"},
		{constant.MakeFloat64(0.1), types.Float32, "1e-01"},
		{constant.MakeInt64(3), types.Float32, "3e+00"},
		{constant.MakeFloat64(1.5), types.Float64, "1.5d+00"},
		{constant.MakeFloat64(0.1), types.Float64, "1d-01"},
		{constant.MakeFloat64(6.02e23), types.UntypedFloat, "6.02d+23"},
		{constant.MakeBool(true), types.Bool, "-1"},
		{constant.MakeBool(false), types.UntypedBool, "0"},
		{constant.MakeString("a\"b\\c\n\té\x01"), types.String, `z" a\"b\\c\n\té\x01"`},
	}
	for _, tt := range tests {
		got := constText(tt.v, types.Typ[tt.t])
		if got != tt.want {
			t.Errorf("constText(%v, %v) = %q, want %q", tt.v, types.Typ[tt.t], got, tt.want)
		}
	}
}

func TestShorten(t *testing.T) {
	if got := shorten("z.short"); got != "z.short" {
		t.Errorf("short name changed: %q", got)
	}
	long := "z." + strings.Repeat("abcdef", 10)
	a, b := shorten(long), shorten(long+"x")
	if len(a) > maxWord || len(b) > maxWord {
		t.Errorf("too long: %q %q", a, b)
	}
	if a == b {
		t.Errorf("different names shortened to the same word %q", a)
	}
	if a != shorten(long) {
		t.Errorf("not deterministic")
	}
}

func TestPeephole(t *testing.T) {
	tests := []struct{ in, want string }{
		{"1 swap swap +", "1 +"},
		{"x @c", "x 1 @@"},
		{"a b >", "a b swap <"},
		{"3 l! 3 l@ 1 +", "dup 3 l! 1 +"},
		{"2 2l@ swap drop", "3 l@"},
		{"2 2l@ drop", "2 l@"},
		{"zgoto 1 zlabel 1", "zlabel 1"},
		{"zgoto 1 zlabel 2", "zgoto 1 zlabel 2"},
		{"x 0 = 0 = if", "x if"},
		{"5 0 +", "5"},
		{"1 l! fi 1 l@", "1 l! fi 1 l@"}, // fi is a jump target: no rewrite across it
	}
	for _, tt := range tests {
		got := strings.Join(peephole(strings.Fields(tt.in)), " ")
		if got != tt.want {
			t.Errorf("peephole(%q) = %q, want %q", tt.in, got, tt.want)
		}
	}
}

// compile compiles a one-file program and returns the Forth.
func compile(t *testing.T, src string, opts Options) string {
	t.Helper()
	res, err := Compile([]Source{{Name: "t.zgo", Text: []byte(src)}}, opts)
	if err != nil {
		t.Fatalf("compile: %v", err)
	}
	return res.Forth
}

func TestFormatRewrite(t *testing.T) {
	src := `package main
import "dev"
func main() {
	var i int32
	var u uint16
	var l int64
	var ul uint64
	var f float32
	var d float64
	var b bool
	var s string
	var bs [4]byte
	dev.Printf("%d %d %d %d|%x %X %x\n", i, u, l, ul, i, ul, u)
	dev.Printf("%5.2f %e %g %G|%v %v %v %v\n", f, d, f, d, b, s, f, l)
	dev.Printf("%s %s %t %c %%|%-8v|%+d\n", s, bs[:], b, i, s, i)
}`
	out := compile(t, src, Options{})
	for _, want := range []string{
		`z" %d %u %ld %lu|%x %lX %x\n" fmt`,
		`z" %5.2f %le %g %lG|%s %s %g %ld\n" fmt`,
		`z" %s %s %s %c %%|%-8s|%+d\n" fmt`,
	} {
		if !strings.Contains(out, want) {
			t.Errorf("format not rewritten as expected; want %s in\n%s", want, out)
		}
	}
	if !strings.Contains(out, "zbool") {
		t.Errorf("bools should be printed with zbool")
	}
}

func TestFrameSlots(t *testing.T) {
	src := `package main
func f(a int32, b int64, c float32) (r int64) {
	x := a
	{
		y := int64(x)
		r = y
	}
	{
		z := b
		r += z
	}
	_ = c
	return
}
func main() { f(1, 2, 3) }`
	out := compile(t, src, Options{NoOptimize: true})
	// parameters take 1+2+1 slots, the named result 2, then x 1, and y
	// and z share the next two
	if !strings.Contains(out, ": z.f 4 9 frame") {
		t.Errorf("unexpected frame layout:\n%s", out)
	}
}

func TestNames(t *testing.T) {
	src := `package main
type T struct{}
func (t *T) Get() int32 { return 1 }
func helper() int32 { return 2 }
func Exported() int32 { return 3 }
var counter int32
func init() {}
func main() {
	var t T
	counter = t.Get() + helper() + Exported()
}`
	out := compile(t, src, Options{})
	for _, w := range []string{": z.T.Get", ": z.helper", ": z-Exported", "constant z.counter", ": z-main", ": z-init", ": z:init"} {
		if !strings.Contains(out, w) {
			t.Errorf("missing %q in\n%s", w, out)
		}
	}
}

func TestErrorsArePositioned(t *testing.T) {
	_, err := Compile([]Source{{Name: "e.zgo", Text: []byte("package main\nfunc main() {\n\tgo main()\n}\n")}}, Options{})
	el, ok := err.(ErrorList)
	if !ok || len(el) != 1 {
		t.Fatalf("want one error, got %v", err)
	}
	if el[0].Pos.Line != 3 || !strings.Contains(el[0].Msg, "goroutines") {
		t.Errorf("unexpected error %v", el[0])
	}
	_ = token.NoPos
}

func TestNoBounds(t *testing.T) {
	src := `package main
var a [4]int32
func get(s []int32, i int) int32 { return s[i] + a[i] }
func main() { get(a[:], 1) }`
	if out := compile(t, src, Options{}); !strings.Contains(out, "?bounds") {
		t.Errorf("bounds checks missing")
	}
	if out := compile(t, src, Options{NoBounds: true}); strings.Contains(out, "?bounds") {
		t.Errorf("bounds checks not dropped")
	}
}
