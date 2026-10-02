# ZGo: a Go-subset language that compiles to zForth

This is the implementation plan for **ZGo**, a strict subset of Go that is
transpiled to zForth source. ZGo is the language for BlueStreak device scripts
and most of the base firmware; zForth stays the low-level layer underneath.

The plan is written to be executed phase by phase, most likely by an AI agent.
Each phase ends with acceptance criteria that must pass before the next one
starts.

## 0. Context

Read this section first: it is what a fresh agent needs to know about why
ZGo exists, what has already been decided, and what zForth provides today.
Also read `AGENTS.md` and `SYSCALLS.md`.

### 0.1 Background
- **Product:** BlueStreak IoT devices that connect to the BlueStreak IoT
  backend. Most are **ESP32-C3** (32-bit RISC-V, **no FPU**: all floating
  point is software), with some ESP32/S3. Firmware uses Arduino and ESP-IDF.
- **zForth** is the on-device scripting engine: small, embeddable, light on
  RAM. The base dictionary is exported on Linux and baked into flash ("ROM").
- **ZGo** is for everything above the lowest layer: developer scripts and most
  of the base firmware. zForth (plus C syscalls) stays underneath.
- **Most ZGo will be written by AI agents.** That drives the design: the
  language must be one agents already know, and must not look like a language
  while behaving differently.
- **The devices are constrained.** Prefer cheap checks; ask before adding
  runtime protection that costs memory or speed.

### 0.2 Why Go
Options considered: a procedural VB.NET subset (rejected: the owner prefers to
avoid Microsoft technologies), a typed Python subset, a C subset,
Pascal/Oberon, Lua/Teal, and non-Microsoft BASICs. Go won because:
- its arithmetic rules already match zForth: integer overflow wraps, division
  truncates toward zero, `MinInt / -1 == MinInt`, shifts by the full width or
  more give 0, and there are no implicit numeric conversions;
- its sized types (`int32 uint32 int64 uint64 float32 float64`) map directly
  onto zForth's cell, double-cell, float and double-float types;
- fixed arrays are built in, so no GC is needed;
- a function declaration without a body is valid Go, which gives host
  bindings without new syntax;
- agents write it fluently, and `go/types` provides the complete type checker.

Python was rejected because its arithmetic differs from the hardware
(unbounded integers, `//` rounds down, no `float32`). Subtle mismatches like
that are exactly what trip up agents.

### 0.3 Decisions already made
Do not revisit these without asking the owner:
- Strict Go subset, `.zgo` files, compiler `zgoc` written in Go on
  `go/parser` + `go/types`, device package `dev`.
- `int`/`uint` are 32-bit. Untyped float constants are `float64`, as in Go.
- Generated code uses numeric local frames (`k l@`), not named locals.
- Go format verbs are rewritten at compile time into zForth `fmt`
  placeholders using each argument's static type.
- No allocation: no `append`/`make`/`new`/maps/string `+`. Arrays are
  package-level in phases 1–3.
- Exported Go functions keep their names as Forth words and are listed in a
  JSON manifest for the firmware.
- Literals in generated code always carry their type explicitly (`1.5e0`,
  `1.5d0`, `123.`).

### 0.4 What zForth provides now
All of this is on branch `bsplatform`. Sources: `src/zforth/zforth.c|h`
(interpreter), `src/zfconf_common.h` (shared config defaults),
`src/linux/` (Linux host: syscalls, `fmt`, the `-H` ROM exporter, the ROM
build), and `forth/`: `core.zf` (upstream core words), `bs.zf` (BlueStreak
words; loads the others; ends with `chkpt empty`), `float.zf`, `double.zf`,
`dfloat.zf`, `memaccess.zf`.

**Cells and numbers**
- A cell is `int32_t`; arithmetic wraps. `<` is a primitive (true signed
  comparison). Unsigned: `u< u> u<= u>= u/ umod rshift umin umax u.`.
- Literals: integers in decimal or `0x` hex from -2147483648 to 4294967295
  (the upper half wraps to negative); floats when the token contains `.` or
  `e`/`E` (`1.5`, `1e3`, `.5`); a trailing dot is a 64-bit double-cell
  integer (`123.`, `-5.`); a `d` exponent is a double float (`1.5d0`).
- Single floats: IEEE bits in one cell. `f+ f- f* f/ f< f= f> f<= f>= f0<
  f0= s>f u>f f>s fsqrt ffloor fceil fround ftrunc fnegate fabs fmin fmax
  f.`. `.` on a float prints its bits.
- 64-bit integers: two cells `( lo hi )`. `d+ d- ud* d= d0= d< du< d> du>
  dmin dmax dumin dumax m* um* d/ dmod ud/ udmod s>d u>d d>s dnegate dabs
  d. ud.`.
- Double floats: two cells `( lo hi )`. `df+ df- df* df/ df< df= df> df<=
  df>= df0< df0= s>df df>s d>df df>d f>df df>f dfsqrt dffloor dfceil dfround
  dftrunc dfnegate dfabs dfmin dfmax df.`.

**Locals**
- Frames on the return stack: `n locals ( x0..xn-1 n -- )` (deepest value is
  local 0), `k l@`, `x k l!`, `k 2l@`, `d k 2l!`, `endlocals`. Recursion
  works; locals stay addressable across `>r` and loops.
- Named locals: `{: a b | c -- comment :}`, `x to name`, `d:` for two-cell
  locals; `;` and `exit` close the frame.

**Memory**
- `var name` (zero-initialized cell), `n variable name`, `2variable`,
  `dfvariable`, `n buffer: name`: all get storage in the RAM data window when
  exported to ROM, so they stay writable.
- `@ !` use variable-length cell encoding. Fixed-size access: `@u8 !u8
  @u16 !u16 @u32 !u32 @s8 ... @c !c` (`c` = one full 4-byte cell). Two-cell:
  `2@ 2!` (high cell at the address, standard order) and `df@ df!` (low cell
  first, native double layout on the little-endian ESP32).
- `cell` (4) and `cells`.

**Output and control**
- `emit`, `.`, `tell ( addr len )`, and `fmt ( args... addr len -- )` with C
  printf conventions (`%d %u %x %c %f %e %g %s`, `l` prefix for two-cell
  arguments; see `SYSCALLS.md`). `fmt" ..."` and `log" ..."` take an inline
  string.
- `if ... else ... fi` (zForth uses `fi`, not `then`), `begin ... again`,
  `begin ... until`, `limit start do ... loop` / `loop+` with `i` and `j`,
  `exit`.
- Checkpoints: `chkpt name` (reusable reset point), `empty` (back to the
  words baked in by `bs.zf`), `marker name` (ANS).

**Missing words** that Forth programmers expect: `then` (use `fi`),
`while`/`repeat`/`leave`, `r@`, `rdrop`, `nip`, `tuck`, `negate`, `abs`,
`invert`, `and`/`or`/`xor` (use `&`/`|`/`^`), `0=`/`0<` (use `=0`/`<0`),
`mod` (use `%`), `c@`/`c!` (use `@u8`/`!u8`), `cmove`, `fill`, `type` (use
`tell`).

**Tests:** `make test` (`tests/run.sh`) builds its own binaries under
`tests/build/`, then runs the unit suites (`tests/suites/`) on the RAM build,
the ROM build and a UBSan build, plus ROM-image tests (`tests/images.sh`),
dictionary-limit tests and a build-config matrix. Add tests there for every
new zForth feature.

### 0.5 Gotchas
- A `)` inside a Forth `( ... )` comment ends the comment early.
- Words are looked up when a definition is compiled, so a word must be
  defined before any definition that uses it.
- Word names are cut to 31 characters by the tokenizer.
- `s"` and `key` currently corrupt bytes above 127 (UTF-8); fixing that is
  phase 0 (6.2).
- **Primitive numbers are baked into compiled code.** Adding a primitive, or
  changing a feature flag that adds or removes primitives (`ZF_ENABLE_FLOAT`,
  `_DFLOAT`, `_DOUBLE_CELL`, `_NAMED_LOCALS`), requires regenerating every ROM
  image, and the Linux exporter and the device must use the same flags.
- `src/linux/main.c` includes `"zforth_dict.h"` with quotes, so it always
  finds the copy in `src/linux/` first; build per-image binaries from a copy of
  `main.c` next to the header (as `tests/lib.sh` does) until 6.3 adds
  `ZF_DICT_HEADER`.
- ESP-IDF defines `int32_t` as `long`: format cells with `PRId32` and
  friends, never a bare `%d`.
- `zf_abort()` is only recoverable inside `zf_eval()`; elsewhere it calls
  `abort()`, which reboots an ESP32.
- On macOS, binaries built with nixpkgs' clang and AddressSanitizer hang at
  startup; the dev shell uses Xcode's compiler there (7.1).

### 0.6 Working conventions
- Work on branch `bsplatform`. Follow `AGENTS.md`: new Forth words go in
  `forth/bs.zf`, or in a new `forth/*.zf` file for a separate set.
- Keep `make test` passing; add tests with every change.
- Ask the owner before committing or pushing.
- Keep this plan current: when a decision changes, update the plan in the same
  change.

## 1. Goals and principles

- **A strict subset of Go.** Every ZGo program is valid Go syntax, and every
  construct means what it means in Go. Anything outside the subset is a
  compile error that names the construct and says what to use instead.
  Agents (and people) already know Go; the risk to avoid is code that looks
  like Go but behaves differently.
- **Static types choose the Forth operators.** zForth is untyped; the
  transpiler knows every expression's type and emits `+`, `f+`, `d+` or
  `df+` accordingly.
- **No garbage collection.** All memory is static: globals, fixed arrays,
  string literals in the dictionary, locals in return-stack frames.
- **Errors at compile time, on the build machine.** Type errors and unsupported
  constructs never reach a device.
- **A fast local loop.** Every example compiles and runs on the Linux zForth,
  both as generated Forth source and as a standalone binary with a ROM image,
  so changes can be tested without hardware.

Non-goals for now: goroutines, channels, maps, interfaces (beyond the `any`
used by formatting), generics, closures, `defer`, `panic`/`recover`,
packages other than `main` and the device package, and matching Go's exact
output for `%v` on floats.

## 2. Naming and layout

| Item | Name |
| --- | --- |
| Language | ZGo |
| Source files | `*.zgo` (valid Go syntax; the extension keeps tools and agents from treating them as ordinary Go) |
| Compiler | `zgoc` |
| Device package | `dev` (imported as `"dev"`) |
| Runtime support words | `forth/zgo.zf` |

```
zgo/                     Go module for the transpiler
  cmd/zgoc/main.go       CLI
  compiler/              parse, check, validate, lower, emit
  dev/dev.zgo            device package declarations (host bindings)
  testdata/errors/       programs that must fail, with expected messages
examples/<name>/         example programs (see section 8)
forth/zgo.zf             runtime support words used by generated code
LANGUAGE.md              the ZGo reference (written in phase 5)
```

## 3. Architecture

The transpiler is written **in Go**, on top of the standard library:

1. **Parse** with `go/parser` (`parser.ParseComments`, to read directives).
2. **Type-check** with `go/types`, configured for a 32-bit target:
   `types.Config{Sizes: &types.StdSizes{WordSize: 4, MaxAlign: 4}, Importer: <custom>}`.
   The custom importer resolves `"dev"` by type-checking `zgo/dev/dev.zgo`.
   This gives Go's exact rules for untyped constants, conversions, operator
   typing and constant folding (`types.Info.Types[expr].Value`), plus error
   messages with positions.
3. **Validate the subset**: walk the typed AST and reject everything outside
   section 4, with targeted messages (section 4.12).
4. **Lower** to a small typed intermediate form: functions, frame slots,
   structured control flow, typed operations.
5. **Emit** zForth source, plus a JSON manifest of exported functions.

Directive comments (`//zf:forth word`) must be read from the raw
`ast.CommentGroup.List`; `CommentGroup.Text()` drops directive-style comments.

## 4. Language subset

### 4.1 Program structure
- `package main` only. A program is one or more `.zgo` files compiled together.
- The only import allowed is `"dev"`.
- `func init()` runs once when the program is loaded. `func main()` is
  optional; it is called by `zgoc run` and by example binaries, and on a device
  only if the firmware chooses to call it.
- **Exported functions** (capitalized names) are entry points the base firmware
  can call by name. They keep their Go name as the Forth word name, and are
  listed in the manifest. Unexported names are mangled (section 5.1).

### 4.2 Types

| ZGo type | Cells | Notes |
| --- | --- | --- |
| `int32`, `int`, `rune` | 1 | `int` is 32-bit, as in TinyGo |
| `uint32`, `uint`, `uintptr` | 1 | |
| `int8`, `int16`, `uint8`/`byte`, `uint16` | 1 | results re-narrowed after arithmetic (5.4) |
| `int64`, `uint64` | 2 | `( lo hi )` double cell |
| `float32` | 1 | IEEE single |
| `float64` | 2 | IEEE double; software on ESP32-C3. Untyped float constants default to `float64`, as in Go |
| `bool` | 1 | Forth flags: 0 / -1 |
| `string` | 2 | `( addr len )`, read-only |
| `[N]T` | address | fixed array in memory |
| `[]T` | 2 | `( addr len )` view of an array; no `append`, `make` or growth |
| named types (`type Celsius float32`) | as underlying | |
| `struct` | — | phase 4 |

Pointers are not supported (phase 4 may add them for structs).

### 4.3 Declarations
- Package-level `var` becomes static storage in the data window, so it stays
  writable when the program is baked into ROM. Initial values must be
  constants.
- Local `var` and `:=` become frame slots (section 5.2).
- `const` and `iota` are folded at compile time.
- **Arrays are package-level only in phases 1–3.** Local arrays need
  addressable frame memory; phase 4 decides between per-function static
  storage and a data-stack frame area.

### 4.4 Functions
- Parameters and results of any 1- or 2-cell type, or slices/strings.
- **Multiple results** are supported: they are simply left on the stack, and
  `a, b := f()` pops them into slots.
- Recursion works (frames are per call).
- **Arrays are not passed by value.** Pass a slice (`f(buf[:])`); passing an
  array value is a compile error that suggests the slice.
- No variadic functions except the formatting functions in `dev`.
- No function values, closures or methods (methods: phase 4).

### 4.5 Statements
- Assignment, `op=`, `++`, `--`, multiple assignment (`a, b = b, a`).
- `if` / `else if` / `else`, including `if x := f(); x > 0 {`.
- `for` in every Go form: three-clause, condition-only, infinite,
  `for i := range n` (Go 1.22 integer range), and
  `for i, v := range arrayOrSlice`.
- `switch` with an optional init statement and tag, `case a, b:`,
  `default`, tagless `switch { case x > 0: }`. No `fallthrough` (phase 4).
- `break`, `continue` (labels: phase 4), `return`.
- Rejected: `goto`, `select`, `go`, `defer`, `fallthrough`.

### 4.6 Expressions
- Arithmetic `+ - * / %`, bitwise `& | ^ &^ << >>`, unary `- + ^ !`,
  comparisons, `&&` and `||` (short-circuit).
- Conversions between all numeric types, `string(byteSlice)`,
  `[]byte(stringValue)` is rejected (it would allocate).
- Indexing and slicing of arrays, slices and strings; `len`; `min`/`max`.
- String `==`, `!=`. String `+` is rejected (it would allocate): use
  `dev.Format`.
- Integer semantics match Go: wraparound on overflow, division truncates
  toward zero, `MinInt / -1 == MinInt`, division by zero aborts. One
  difference: a negative shift count gives 0 instead of panicking.
- Array, slice and string indexes are bounds-checked; an out-of-range index
  aborts the current call, as a Go panic would.

### 4.7 The `dev` package
`zgo/dev/dev.zgo` declares the device API as bodyless functions bound to
zForth words:

```go
package dev

//zf:forth fmt-print
func Printf(format string, args ...any)

//zf:forth fmt-buf
func Format(buf []byte, format string, args ...any) int   // returns bytes written, truncates

//zf:forth ms
func Delay(ms uint32)

//zf:forth millis
func Millis() uint64
```

Programs can bind their own host words the same way:

```go
//zf:forth led-on
func LedOn()
```

**Checked:** `go/types` accepts bodyless function declarations (the Go spec
allows them; only `cmd/compile` rejects them), so no dummy bodies are needed.
With `WordSize: 4`, `i := 0` has type `int` of size 4, and `x := 1.5` is
`float64`.

### 4.8 Formatting
`dev.Printf` and `dev.Format` take Go format strings, which must be constant.
The transpiler rewrites each verb into the Forth `fmt` placeholder for the
argument's static type, keeping flags, width and precision:

| Go verb | Argument type | Forth `fmt` |
| --- | --- | --- |
| `%d` | signed / unsigned 32-bit | `%d` / `%u` |
| `%d` | `int64` / `uint64` | `%ld` / `%lu` |
| `%x`, `%X` | 32 / 64-bit integer | `%x` / `%lx` (same for `X`) |
| `%f %e %g` | `float32` / `float64` | `%f` / `%lf` (etc.) |
| `%v` | integer / float / string / bool | `%d`-family / `%g` / `%s` / `%s` of "true"/"false" |
| `%s` | `string`, `[]byte` | `%s` |
| `%c` | integer | `%c` |
| `%t` | `bool` | `%s` of "true"/"false" |
| `%q %T %p %b %o %U` | | compile error |

`%v` on floats prints like C `%g`, not Go's shortest representation (a
documented difference).

### 4.9 Strings
- String literals live in the dictionary (read-only, also in ROM) and support
  Go escapes, compiled byte-exactly (UTF-8 safe) by the runtime word in 6.1.
- `string(buf[:n])` makes a string view of a byte slice without copying. The
  view changes if the buffer changes; document this.
- Iterating with `for i, b := range s` yields bytes, not runes (a documented
  difference; `rune` decoding can come later).

### 4.10 Entry points and the manifest
`zgoc build` writes `<name>.json` next to the Forth output:

```json
{
  "exports": [
    { "name": "OnTemperature",
      "params": [ { "name": "t", "type": "float32", "cells": 1 },
                  { "name": "ts", "type": "uint64", "cells": 2 } ],
      "results": [] }
  ]
}
```

The base firmware uses it to push arguments (in order; 2-cell values as
`lo hi`) and call the word by name.

### 4.11 Runtime errors
Division by zero, bounds-check failures and stack overflow abort the current
call (`zf_eval` returns an error), like an unrecovered Go panic but without
taking down the firmware.

### 4.12 Targeted error messages
The validator recognizes common out-of-subset constructs and reports a fix:

| Construct | Message |
| --- | --- |
| `go f()`, channels, `select` | goroutines and channels are not supported |
| `append`, `make`, `new`, maps | no dynamic allocation: use a fixed array (`var buf [64]byte`) |
| `s1 + s2` on strings | string concatenation allocates: use `dev.Format` |
| `fmt.Println`, `fmt.Sprintf` | use `dev.Printf` / `dev.Format` |
| `defer`, `panic`, `recover` | not supported |
| interfaces, methods, generics | not supported (methods: planned) |
| closures, function values | not supported |
| local array | declare arrays at package level |
| array passed by value | pass a slice: `f(a[:])` |
| non-constant format string | format strings must be constants |
| unsupported format verb | lists the supported verbs |

## 5. Code generation

### 5.1 Names
- Exported functions keep their name (`OnTemperature`). zForth is
  case-sensitive and its own words are lowercase, so they cannot collide.
- Everything else is mangled with a `z.` prefix (`z.count`, `z.helper`). Names
  longer than 31 characters (zForth's limit) are shortened to a prefix plus a
  short hash, deterministically.
- String literals become `z.s<N>`-style constants or inline literals.

### 5.2 Functions and locals
Each function uses one numeric frame: parameters first, then every local
(including temporaries), with 2-cell values taking two slots:

```forth
( func Average(a, b float32) float32 )
: z.average  2 locals  0 l@ 1 l@ f+ 2.0e0 f/ ( see 5.3 ) endlocals ;
```

- Locals use `k l@` / `k l!` / `k 2l@` / `k 2l!`. Named `{: :}` locals are not
  used: numeric slots avoid the 31-character and name-table limits.
- Every exit path emits `endlocals` before `exit`.
- Functions without parameters or locals skip the frame entirely.
- Results are left on the data stack in order.

### 5.3 Expressions
Post-order emission; the static type selects the word:

| Operation | int32 | uint32 | int64 / uint64 | float32 | float64 |
| --- | --- | --- | --- | --- | --- |
| `+ -` | `+ -` | `+ -` | `d+ d-` | `f+ f-` | `df+ df-` |
| `*` | `*` | `*` | `ud*` | `f*` | `df*` |
| `/ %` | `/ %` | `u/ umod` | `d/ dmod` / `ud/ udmod` | `f/` | `df/` |
| `<` | `<` | `u<` | `d<` / `du<` | `f<` | `df<` |
| `==` | `=` | `=` | `d=` | `f=` | `df=` |

- Literals: integers as decimal, `int64` as `123.`, `float32` with an
  exponent (`1.5e0`), `float64` with a `d` exponent (`1.5d0`), so the type
  never depends on the zForth parser's defaults.
- Conversions: `s>f u>f f>s s>d u>d d>s s>df d>df df>d df>s f>df df>f`, plus
  narrowing (5.4).
- `&&` / `||` compile to `if ... else ... fi`.
- `x op= y`, `x++` read, compute and store the slot or global.

### 5.4 Narrow integers
After arithmetic on `uint8`/`uint16`, mask (`255 &`, `65535 &`); for
`int8`/`int16`, sign-extend (`24 << 24 >>`, `16 << 16 >>`). Memory access to
`[N]byte` arrays uses `@u8` / `!u8`, and so on for other widths.

### 5.5 Globals and arrays
- Scalars: `var` (1 cell) or `2variable` (2 cells) in the data window,
  initialized at load with the constant value.
- Arrays: `N * size buffer:`, accessed with the typed memory words
  (`@u8 !u8 @c !c`, and `2@ 2!` / `df@ df!` for 8-byte elements), behind a
  bounds check.

### 5.6 Control flow
Generated code uses structured words from `forth/zgo.zf` (6.1):
`if ... else ... fi`, loops with `begin`/`while`/`repeat` and `again`, and
`break`/`continue` that jump to the innermost loop's exit or continue point.
`switch` lowers to a temporary slot and a chain of `if`s.

### 5.7 Calls
- ZGo functions: push the arguments, call the word.
- Host functions: push the arguments, call the bound word.
- `dev.Printf`/`dev.Format`: push the arguments, push the rewritten format
  string, call `fmt` / the buffer variant.

## 6. zForth prerequisites (phase 0)

### 6.1 Runtime library `forth/zgo.zf`
- `while`, `repeat` (zForth core only has `begin`/`again`/`until`), and
  `break`/`continue` for the innermost loop, using a compile-time patch list
  with `,j` / `!j`.
- `z"`: a string literal word with Go escapes (`\n \t \r \" \\ \xHH`) that
  compiles bytes exactly (`,u8`-style) so UTF-8 works.
- Bounds check: `( i n -- i )`, aborting if `i` is not in `0..n-1`.
- Byte copy (`cmove`) and string compare (`str=`).
- A buffer-formatting variant of `fmt` for `dev.Format` (new syscall, see 6.3).

### 6.2 Fix `s"` and `key` for non-ASCII bytes
`key` returns `char` values sign-extended, and `s"` stores characters with
`,` (variable-length cells), so `s" hé!"` currently yields a 12-byte garbage
string. Make `key` push the unsigned byte and `s"` store raw bytes.

### 6.3 Linux host and build support
- **`-e WORD`** option for `zforth` / `zforth-rom`: execute a word after
  loading (used to call `main` and to drive example handlers).
- **Header selection:** `main.c` includes `"zforth_dict.h"` with quotes, which
  always finds the copy next to `main.c` first. Add `-DZF_DICT_HEADER=<path>`
  (`#include ZF_DICT_HEADER`) so per-example images can be built.
- **`fmt` into a buffer** (syscall `ZF_SYSCALL_USER + 5`, now free):
  `( args... buf-addr buf-len fmt-addr fmt-len -- n )`, same placeholders as
  `fmt`, truncating; document it in `SYSCALLS.md`.

**Acceptance:** tests in `tests/suites/` for each new word and option;
`s" hé!" tell` prints `hé!` and has length 4; `make test` passes.

## 7. Toolchain

### 7.1 Development environment
The repo has a nix flake with a dev shell; with direnv, entering the directory
loads it (`direnv allow` once), or run `nix develop`. It provides Go (1.26 at
the time of writing), `gopls` and `make`.

- **macOS:** the shell deliberately adds no C toolchain, so zForth keeps
  building with Xcode's compiler. Binaries built with nixpkgs' clang and
  `-fsanitize=address` hang at startup on macOS, and the zForth build relies
  on AddressSanitizer.
- **Linux:** the shell uses nixpkgs' gcc and readline.
- `src/linux/Makefile` uses `$(CC)` from the environment when one is set, and
  `gcc` otherwise.

### 7.2 Commands

```
zgoc build [-o out.zf] [--manifest out.json] file.zgo...
zgoc run   file.zgo...            # build, then run on the Linux zforth
zgoc image -o build/name file.zgo...   # standalone Linux binary with a ROM image
zgoc test  examples/...           # build, run, compare with expected output
```

- **`build`** writes the Forth source and manifest.
- **`run`** executes
  `src/linux/zforth forth/bs.zf forth/zgo.zf [host.zf] out.zf -e main`
  (adding `-e main` only if `main` exists) and streams the output.
- **`image`** produces a firmware-like binary:
  1. `zforth -H zgo_image forth/bs.zf forth/zgo.zf [host.zf] out.zf > build/name/image.h`
  2. compile `src/linux/main.c` and `src/zforth/zforth.c` with
     `-DZF_ENABLE_ROM_DICT=1 -DZF_LINUX_ROM_DICT=1 -DZF_DICT_HEADER='"build/name/image.h"'`
     into `build/name/name`
  3. the binary mounts the image from "ROM" exactly as a device would; run it
     with `-e main`, or feed handler calls on stdin.
- **`test`** runs each example in both `run` and `image` mode and requires
  identical output matching `expected.txt`.
- A top-level `make examples` builds every example image; `make test-zgo`
  runs `zgoc test examples/...` plus the error-case tests.

## 8. Examples

Each example lives in `examples/<name>/` with:
- `main.zgo` (and more `.zgo` files if useful);
- `expected.txt`: exact expected output;
- optionally `host.zf`: Linux stand-ins for host words the example binds
  (for instance `: led-on ." [led on]" cr ;`);
- optionally `calls.txt`: Forth lines run after loading, to simulate the
  firmware calling exported handlers (e.g. `21.5e0 1700000000000. OnTemperature`).

Create these, in this order, each exercising one area:

| # | Example | Demonstrates |
| --- | --- | --- |
| 1 | `hello` | `dev.Printf`, `main` |
| 2 | `numbers` | every numeric type, wraparound, conversions, `int64` timestamps, `float32` vs `float64` precision |
| 3 | `control` | FizzBuzz: `for`, `if`, `switch`, `break`, `continue` |
| 4 | `functions` | recursion (`fib`, `fact`), multiple results (`divmod`), early return |
| 5 | `arrays` | package-level arrays, slices, `range`, a moving-average ring buffer |
| 6 | `strings` | literals with escapes and UTF-8, `len`, comparison, byte iteration, `dev.Format`, `string(buf[:n])` |
| 7 | `globals` | package `var`s, `init()`, state persisting across calls |
| 8 | `handlers` | exported `OnTemperature(t float32, ts uint64)` and `OnButton(id uint8)` driven by `calls.txt`; checks the manifest |
| 9 | `host` | binding host words with `//zf:forth`, Linux mocks in `host.zf` |
| 10 | `errors` | not an example program: `zgo/testdata/errors/*.zgo`, each with an `// ERROR "..."` comment giving the expected message for each rejected construct in 4.12 |

Example 1, for reference:

```go
// examples/hello/main.zgo
package main

import "dev"

func main() {
	dev.Printf("hello from ZGo: %d\n", 42)
}
```

`expected.txt`:
```
hello from ZGo: 42
```

## 9. Phases

**Phase 0: zForth prerequisites.** Section 6. Go comes from the nix dev
shell (7.1).
*Accept:* `make test` passes, including the new tests.

**Phase 1: core transpiler.** Parse, type-check, validate; scalar types
(all widths, `bool`), package vars, locals, functions with one result, all
operators and conversions, `if`/`for`/`switch`/`break`/`continue`,
`dev.Printf` with verb rewriting, `zgoc build` and `zgoc run`.
*Accept:* examples 1–4 pass in run mode; error tests for constructs used so
far pass.

**Phase 2: data and entry points.** Arrays, slices, strings and escapes,
`dev.Format`, narrow integers, multiple results, `init()`, exported functions
and the manifest, host bindings.
*Accept:* examples 5–9 pass in run mode; all error tests pass.

**Phase 3: images.** `zgoc image`, `zgoc test` in both modes, `make examples`,
`make test-zgo`.
*Accept:* every example builds as a standalone binary and produces identical
output in run and image mode; the binaries also pass under the UBSan build of
zForth.

**Phase 4: language growth (as needed).** Structs (as package-level records,
then locals), methods, labeled `break`/`continue`, `fallthrough`, local
arrays, a peephole optimizer over the emitted Forth.

**Phase 5: documentation.** `LANGUAGE.md`: the subset, every difference from
Go, the `dev` API, a style guide (prefer `float32` where precision allows),
and the error messages. Point to it from `AGENTS.md` so agents read it before
writing ZGo.

## 10. Testing

- **Golden output:** each example's output must match `expected.txt` exactly,
  in both run and image mode.
- **Error tests:** each file in `zgo/testdata/errors/` must fail with the
  message in its `// ERROR` comment.
- **Unit tests** (`go test ./zgo/...`) for verb rewriting, name mangling,
  frame-slot assignment and constant emission.
- **zForth level:** `make test` keeps passing; generated code is also run on
  the UBSan build of zForth (`tests/lib.sh` has the build helpers).
- `make test-zgo` runs the ZGo tests; `make test` should eventually run both.

## 11. Open decisions

1. **Where the compiler runs in production:** on the BlueStreak backend
   (compile, then send Forth source to devices), on developer machines only,
   or both. The plan works either way; the backend option needs a service
   wrapper around `zgoc build`.
2. **Bounds checks:** always on (Go semantics), or a flag to drop them for
   trusted base-firmware code.
3. **`main()` on devices:** whether firmware ever calls it, or only `init()`
   and exported handlers.
4. **Local arrays and structs** (phase 4): static per-function storage
   (simple, not reentrant) versus a frame area in memory.
