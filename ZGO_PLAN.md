# ZGo: a Go-subset language that compiles to zForth

This is the implementation plan for **ZGo**, a strict subset of Go that is
transpiled to zForth source. ZGo is the language for BlueStreak device scripts
and most of the base firmware; zForth stays the low-level layer underneath.

The plan was written to be executed phase by phase. **Status: all phases are
implemented** (section 9). `LANGUAGE.md` is the reference for the language as
built; this plan records the background, decisions and design, and is kept
current as decisions change.

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

Decided in the plan review (2026-10-02):
- **Output need not match Go exactly.** Where zForth's C-style `fmt` differs
  from Go (`%x` of a negative signed integer, `%c` above 127, `%g`/`%v` on
  floats), ZGo documents the difference instead of emulating Go.
- **Public names are `z-<Name>`:** exported functions become `z-OnTemperature`,
  `func init()` becomes `z-init` and `func main()` becomes `z-main`. Both
  `z-init` and `z-main` are always emitted (empty if not defined). Generated
  code never calls them; the firmware calls `z-init` after loading or mounting
  a program, then whatever it needs. Internal names use `z.` (5.1).
- **Slices are two cells `( addr len )` with no capacity:** `cap()` on a slice
  and three-index slices are rejected; re-slicing past a slice's length aborts
  (in Go it is allowed up to the capacity).
- **`break` inside `switch` exits the switch**, as in Go; `continue` targets
  the enclosing loop.
- **Function order:** Go allows any order, zForth binds words when compiled,
  so functions are emitted in call-graph order; mutually recursive functions
  call through forward-declared stubs.
- **New syscalls only for simple, common needs:** `ZF_SYSCALL_USER + 5`
  (`fmt` into a buffer), `+ 6` (`ms`, delay) and `+ 7` (`millis`).
- **New Forth words** of general use go in `bs.zf`; words only generated code
  uses go in `forth/zgo.zf`.
- **Bounds checks** use a new `?bounds` primitive and a new abort reason
  (`ZF_ABORT_BOUNDS`).

### 0.4 What zForth provides now
All of this is on branch `bsplatform`. Sources: `src/zforth/zforth.c|h`
(interpreter), `src/zfconf_common.h` (shared config defaults),
`src/linux/` (Linux host: syscalls, `fmt`, the `-H` ROM exporter, the ROM
build), and `forth/`: `core.zf` (upstream core words), `bs.zf` (BlueStreak
words; loads the others; ends with `chkpt empty`), `float.zf`, `double.zf`,
`dfloat.zf`, `memaccess.zf`, and `zgo.zf` (runtime words for generated ZGo
code; loaded after `bs.zf`).

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
  local 0), `p n frame` (p values from the stack, the rest zero), `k l@`,
  `x k l!`, `k 2l@`, `d k 2l!`, `endlocals`. Recursion works; locals stay
  addressable across `>r` and loops.
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
- `cell` (4) and `cells`. `src dst n move` (overlap-safe), `addr n byte
  fill`, `cmove`, `c@ c! c,`.

**Output and control**
- `emit`, `.`, `tell ( addr len )`/`type`, and `fmt ( args... addr len -- )`
  with C printf conventions (`%d %u %x %X %c %f %e %E %g %G %s`, `l` prefix
  for two-cell arguments; see `SYSCALLS.md`); `fmt-buf` formats into a
  buffer. `fmt" ..."` and `log" ..."` take an inline string. `n ms` waits,
  `millis` gives a 64-bit millisecond count.
- `if ... else ... fi` (or `then`), `begin ... again`, `begin ... until`,
  `begin ... while ... repeat`, `limit start do ... loop` / `loop+` with `i`
  and `j`, `exit`, `abort`.
- Checkpoints: `chkpt name` (reusable reset point), `empty` (back to the
  words baked in by `bs.zf`), `marker name` (ANS).

**Also in `bs.zf`:** `nip tuck -rot r@ rdrop negate abs invert and or xor
mod lshift 0= 0< 0<> 0> <>`, strings (`str= compare`), 64-bit bitwise ops
and shifts (`d& d| d^ dinvert dlshift drshift darshift`), and conversions
between unsigned or 64-bit integers and floats (`f>u df>u u>df f>d d>f
ud>df df>ud ud>f f>ud`). Still missing: `leave`.

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
- **Primitive numbers are baked into compiled code.** Adding a primitive, or
  changing a feature flag that adds or removes primitives (`ZF_ENABLE_FLOAT`,
  `_DFLOAT`, `_DOUBLE_CELL`, `_NAMED_LOCALS`), requires regenerating every ROM
  image, and the Linux exporter and the device must use the same flags.
- `src/linux/main.c` includes `"zforth_dict.h"` with quotes, so it finds the
  copy in `src/linux/` first; build per-image binaries with
  `-DZF_DICT_HEADER='"path/image.h"'` (as `zgoc image` does) or from a copy of
  `main.c` next to the header (as `tests/lib.sh` does).
- `bs.zf` includes its files by relative path: run `zforth` from the repo
  root.
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

`LANGUAGE.md` is the reference for the language as built: what is supported,
every difference from Go and every rejected construct. This section records
the design decisions behind it.

### 4.1 Program structure
- `package main` only, one or more `.zgo` files compiled together; the only
  import is `"dev"`.
- Loading a program runs nothing but constant initializers. The firmware
  calls `z-init` (non-constant package initializers in Go's dependency
  order, `types.Info.InitOrder`, then every `init` in source order), and
  `z-main` if it wants `main`. Both always exist.
- Exported functions are `z-Name`; exported package variables are `z-Name`
  too, pushing the variable's address. Both are listed in the manifest.

### 4.2 Types

| ZGo type | Cells | Memory | Notes |
| --- | --- | --- | --- |
| `int32`, `int`, `rune` | 1 | 4 | `int` is 32-bit, as in TinyGo |
| `uint32`, `uint`, `uintptr` | 1 | 4 | |
| `int8`, `int16`, `uint8`/`byte`, `uint16` | 1 | 1, 2 | re-narrowed after arithmetic (5.4) |
| `int64`, `uint64` | 2 | 8 | `( lo hi )` |
| `float32` | 1 | 4 | |
| `float64` | 2 | 8 | software on ESP32-C3; untyped float constants default to it |
| `bool` | 1 | 1 | flags 0 / -1; stored as a byte, loaded sign-extended |
| `string` | 2 | 8 | `( addr len )` |
| `[N]T` | address | N × size | |
| `[]T` | 2 | 8 | `( addr len )`, no capacity |
| `struct` | address | fields in order, aligned to ≤ 4 | |
| `*T` | 1 | 4 | |

In memory, two-cell values are stored low cell first (`df@ df!`), matching
the ESP32's little-endian layout for C interop.

### 4.3 Declarations and storage
- Package variables share one static data block (`N buffer: z:data`, so it
  is in the RAM data window of a ROM image); each variable is a constant
  word for its address. The block is zero-filled at load time.
- Constant initializers, and composite literals made only of constants
  (including package-level slice and `&T{...}` literals), are stored at load
  time and so baked into ROM; the rest run in `z-init`.
- Local scalars live in the numeric frame (5.2).
- **Local arrays and structs, and composite literals inside functions, use
  static storage** in the data block (open decision 11.4, resolved): one area
  per declaration or literal, zeroed or filled whenever the declaration or
  literal executes. Recursive functions (any function in a call cycle)
  cannot use them: the compiler rejects it, since static storage is not
  reentrant.

### 4.4 Functions and methods
- Parameters and results of any 1- or 2-cell type; multiple and named
  results; `f(g())`.
- Arrays are not passed by value (pass a slice) and arrays and structs are
  not returned.
- **Struct parameters and struct value receivers are passed by address and
  must be read-only**; the compiler rejects assignments to them, `&` of them
  and slicing their arrays. Other value receivers are copies.
- Methods are words named `z.Type.method`, with the receiver as the first
  argument.
- Recursion works; mutual recursion goes through call vectors (5.1).
- No variadic functions (except `dev.Printf`/`dev.Format`), function values,
  closures, method values or generics.

### 4.5 Statements
Everything in Go except `goto`, `go`, `defer`, `select` and channel sends:
labeled `break`/`continue` and `fallthrough` are supported, `break` inside a
`switch` leaves the switch, and `for i := range n` follows Go 1.22 (the
compiler keeps a hidden counter, so changing `i` does not change the count).

### 4.6 Expressions
- Every operator; integer semantics match Go, except that a negative shift
  count gives 0 (or -1 for `>>` of a negative value) instead of panicking.
- Conversions between all numeric types; `string(byteSlice)` is a view;
  `[]byte(s)`, `string(rune)` and string `+` are rejected.
- Builtins: `len`, `cap` (arrays), `min`, `max`, `copy`, `print`, `println`,
  `panic`.
- Index checks with the `?bounds` primitive (open decision 11.2, resolved:
  on by default, `zgoc -nobounds` drops them).

### 4.7 The `dev` package
`zgo/dev/dev.zgo` (embedded in the compiler) declares `Printf`, `Format`,
`Print` (bound to `tell`), `Delay` (`ms`) and `Millis` (`millis`). `Printf`
and `Format` have no `//zf:forth` binding: the compiler handles them (4.8).
Programs bind their own host words with `//zf:forth`; the directive text is
emitted verbatim after the arguments, so it may be any Forth.

### 4.8 Formatting
Format strings must be constant. Each verb is checked against the static
type of its argument and rewritten into a zForth `fmt` placeholder (`%d` of
a `uint16` becomes `%u`, of an `int64` `%ld`; `%f` of a `float64` `%lf`;
`%v` picks by type; bools print through `zbool` as `%s`). Supported: `%d %x
%X %c %f %F %e %E %g %G %s %v %t %%` with flags, width and precision. One
call takes at most 16 argument cells (the Linux host's limit). Output
differences from Go are documented, not emulated (decision 0.3).

### 4.9 Strings
Literals are compiled byte-exactly with `z"`. `for i, r := range s` decodes
UTF-8 runes exactly as Go does (`zrune` in `zgo.zf`); this replaced the
earlier plan to iterate bytes, since decoding is cheap and avoids a
difference from Go.

### 4.10 Entry points and the manifest
`zgoc build` writes `<name>.json` next to the Forth: `init`, `main`, the
exported functions with their parameters and results (name, type, cells),
and the exported variables (name, word, type, size). See `LANGUAGE.md` 2.

### 4.11 Runtime errors
Index errors (`ZF_ABORT_BOUNDS`), division by zero, `panic` (prints
`panic: v`, then the `abort` primitive: `ZF_ABORT_USER`) and stack overflow
abort the current `zf_eval` call. Nil pointers are not checked (a check per
dereference costs speed; see 11.5).

### 4.12 Targeted error messages
`compiler/validate.go` checks the subset in two passes: syntax-only checks
before type checking (so `import "fmt"` or `go f()` is not buried under type
errors), then type-aware checks. Code generation reports the rest (local
arrays in recursive functions, writes to read-only struct parameters, `&` of
a frame local, over-long exported names). `zgo/testdata/errors/*.zgo` has a
case for every message, marked `// ERROR "text"`.

## 5. Code generation

The compiler (`zgo/compiler`) generates Forth directly from the typed AST:
`gen.go` (program, names, call graph, data block), `func.go` (statements and
frames), `expr.go` (expressions), `format.go` (formatting), `emit.go`
(peephole and load-time initialization), `manifest.go`.

### 5.1 Names and order
- `z-Name` for exports, `z-init`, `z-main`; `z.name` for other functions and
  variables; `z.Type.method`; `z.func.var` for static locals; `z:...` for
  compiler-made words (`z:data`, `z:lit`, `z:init`, `z:xt.f`), which cannot
  clash with Go identifiers. Names over 31 bytes are cut and given a hash
  suffix; exported names over 29 characters are an error.
- Functions are emitted in call-graph order (Tarjan's strongly connected
  components, callees first). In a cycle of several functions, each gets a
  stub `: z.f z:xt.f @c zexec ;` first, and after its real definition
  `' z.f z:xt.f !c` sets the call vector. Self-recursion needs nothing.

### 5.2 Functions and locals
One numeric frame per function: parameters, then named results, then
locals and temporaries, two slots for two-cell values. Slots are reused
after a block ends. The prologue is `n locals` when all slots are
parameters, otherwise `p n frame` (a new primitive: `p` slots from the
stack, the rest zeroed). Every `return` before the end emits
`endlocals exit`; functions with no slots have no frame.

### 5.3 Expressions
Post-order, with the static type choosing the word (`+ d+ f+ df+`, `/ u/ d/
ud/`, `< u< d< du< f< df<`, ...). Literals always carry their type: `42`,
`42.`, `1.5e+00`, `1.5d+00`. Assignments follow Go's order (index operands,
then values, then stores left to right), using a temporary or a `swap`/`rot`
only when side effects make the order visible.

### 5.4 Narrow integers
After `+ - * <<` (and signed `/`, unary `-`, and `^` on unsigned) on 8- and
16-bit types: `255 &` / `65535 &`, or `24 << 24 >>` / `16 << 16 >>`. Memory
uses `@u8 @s8 @u16 @s16 !u8 !u16`.

### 5.5 Data and arrays
Element access: `base i n ?bounds size * +` for arrays (constant indexes
fold to an offset, no check), `addr len i swap ?bounds size * +` for slices
and strings; slicing goes through `zslice`, `copy` through `zcopy`, struct
and array assignment through `move`, zeroing through `fill`.

### 5.6 Control flow
`if ... else ... fi` for `if`, `&&`, `||` and switches without
`fallthrough`. Loops and `fallthrough` switches use jump labels from
`zgo.zf`: `znew N`, `zgoto N`, `zgoto0 N`, `zlabel N` (forward references
are chained through the jump cells and resolved when the label is placed;
32 label numbers per function, reused once a construct closes). A loop is
`begin cond zgoto0 B body zlabel C post again zlabel B`.

### 5.7 Calls and the peephole pass
Arguments are pushed in order, then the word (or the host binding's text).
The peephole pass (`-O0` turns it off) inlines short words as primitives
(`@c` → `1 @@`, `>` → `swap <`, `<=` → `swap < 0 =` ...), removes no-ops
(`swap swap`, `0 +`, a jump to the next instruction), turns `k l! k l@`
into `dup k l!` and `k 2l@ swap drop` into `k+1 l@`. Rules never match
across a jump target (`fi`, `zlabel`, `begin`, `else` are code tokens).

## 6. zForth prerequisites (phase 0): done

- **Primitives** (in `zforth.c`, so ROM images must be regenerated):
  `frame`, `?bounds` (with `ZF_ABORT_BOUNDS`), `move`, `fill`, `abort` (with
  `ZF_ABORT_USER`), and `d>f`/`ud>f` (64-bit integers to single floats,
  rounded once; through a double they rounded twice).
- **`bs.zf`**: `nip tuck -rot r@ rdrop negate abs invert and or xor mod
  lshift 0= 0< 0<> 0> <> c@ c! c, type then while repeat cmove str= compare
  d& d| d^ dinvert dlshift drshift darshift u>df f>u df>u f>d ud>df df>ud
  f>ud fmt-buf ms millis`.
- **`forth/zgo.zf`**: the label words, `z"` (Go escapes, byte-exact),
  `zbool`, `zslice`, `zcopy`, `zcnt`, `zexec`, `zrune`.
- **Fixes**: `key` returns unsigned bytes and `s"` stores raw bytes, so UTF-8
  works; `s"` of 128 bytes or more compiled correctly only by accident (the
  length cell is now fixed-size); a five-byte cell straddling the end of the
  allocated dictionary aborted instead of growing it; `isspace` on bytes
  above 127 was undefined behavior.
- **Linux host**: `-e WORD` and files run in command-line order, `-x` exits
  instead of reading stdin, the exit status reports failures, input lines up
  to 4 KB, `-DZF_DICT_HEADER`, syscalls `fmt-buf` (`USER + 5`), `ms` (`+ 6`),
  `millis` (`+ 7`), and `%E %G` in `fmt`. Documented in `SYSCALLS.md`.

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
`zgoc build | run | image | test`, described in `LANGUAGE.md` 16. `image`
exports `bs.zf`, `zgo.zf`, `host.zf` and the program with `zforth -H`, then
compiles `src/linux/main.c` and `zforth.c` with `-DZF_DICT_HEADER` (and
`-san asan|ubsan` if asked). `make zgoc`, `make examples`, `make test-zgo`;
`make test` runs the zForth and ZGo tests.

## 8. Examples

`examples/<name>/` has `main.zgo`, `expected.txt`, the committed generated
`main.zf` and `main.json` (checked by `zgoc test`), and optionally `host.zf`
(Linux stand-ins for bound host words) and `calls.txt` (Forth lines playing
the firmware). `make examples` builds each as `build/examples/<name>/<name>`.

| Example | Demonstrates |
| --- | --- |
| `hello` | `dev.Printf`, `main` |
| `numbers` | every numeric type, wraparound, conversions, `int64` timestamps, `float32` vs `float64` |
| `control` | FizzBuzz, `switch`, `break`, `continue`, labels, `fallthrough`, every `for` form |
| `functions` | recursion, mutual recursion, multiple and named results, early return |
| `arrays` | package-level arrays, slices, `range`, `copy`, 2D arrays, a ring buffer |
| `strings` | escapes and UTF-8, comparison, runes, `dev.Format`, `string(buf[:n])` views |
| `globals` | package variables, non-constant initializers, `init`, state across calls |
| `handlers` | exported handlers driven by `calls.txt`, an exported variable, the manifest |
| `host` | `//zf:forth` bindings with `host.zf` stand-ins, inline Forth |
| `structs` | structs, nested structs, methods, pointer and value receivers, pointers |
| `thermostat` | a complete device script: handlers, state machine, JSON via `dev.Format`, host words |

All examples except `strings` (which shows the `string(buf[:n])` view
difference) and those using host words print exactly what real Go prints.

## 9. Phases

All phases are done:

- **Phase 0** (zForth prerequisites, section 6).
- **Phase 1** (core transpiler) and **phase 2** (data and entry points).
- **Phase 3** (images): every example runs identically in run and image
  mode, and the images pass under UBSan.
- **Phase 4** (language growth): structs (package-level and local), methods,
  pointers, labeled `break`/`continue`, `fallthrough`, local arrays (static
  storage), the peephole optimizer; also `panic`, `print`/`println`, UTF-8
  `range`, exported variables.
- **Phase 5** (documentation): `LANGUAGE.md`, linked from `AGENTS.md`.

## 10. Testing

- `make test` runs everything: the zForth suites (`tests/run.sh`, including
  `words`, `zgort` and `host` for the phase 0 additions) and `make test-zgo`.
- `go test ./...` in `zgo/`: unit tests (constants, names, frames, format
  rewriting, peephole) and **`TestCompat`**, which runs each program in
  `zgo/testdata/compat` with ZGo, with and without the peephole pass, and
  with the real Go toolchain, and requires identical output. Add a compat
  program for any new language feature that Go can run.
- `zgoc test examples/* zgo/testdata/errors`, then the example images again
  under UBSan.

## 11. Open decisions

1. **Where the compiler runs in production:** on the BlueStreak backend
   (compile, then send Forth source to devices), on developer machines only,
   or both. Either works; the backend option needs a service wrapper around
   `zgoc build` (the `compiler` package can be called directly).
2. ~~Bounds checks~~: on by default, `-nobounds` to drop them.
3. ~~`main()` on devices~~: the firmware decides; `z-main` always exists.
4. ~~Local arrays and structs~~: static storage, rejected in recursive
   functions.
5. **Nil pointer checks:** not done (a check per dereference costs speed).
   Could be added as a compile flag if nil dereferences become a problem.
6. **Return stack size for ZGo firmware:** frames live on the return stack
   (128 cells by default); firmware written in ZGo with deep call chains may
   want a larger `ZF_RSTACK_SIZE`.
