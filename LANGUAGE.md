# ZGo language reference

ZGo is a strict subset of Go that compiles to zForth. It is the language for
BlueStreak device scripts and most of the base firmware. Every ZGo program is
valid Go: if you know Go, you know ZGo, and this document lists exactly where
the two differ and what ZGo leaves out.

**The rule:** anything ZGo accepts means what it means in Go, except for the
documented differences in [section 12](#12-differences-from-go). Anything
outside the subset is a compile error that names the construct and says what
to use instead ([section 13](#13-not-supported)).

Contents:

1. [Quick start](#1-quick-start)
2. [Programs and entry points](#2-programs-and-entry-points)
3. [Types](#3-types)
4. [Declarations and storage](#4-declarations-and-storage)
5. [Functions and methods](#5-functions-and-methods)
6. [Statements](#6-statements)
7. [Expressions and operators](#7-expressions-and-operators)
8. [Strings](#8-strings)
9. [The dev package](#9-the-dev-package)
10. [Formatting](#10-formatting)
11. [Runtime errors](#11-runtime-errors)
12. [Differences from Go](#12-differences-from-go)
13. [Not supported](#13-not-supported)
14. [Device limits](#14-device-limits)
15. [Style guide for device code](#15-style-guide-for-device-code)
16. [Tools](#16-tools)

## 1. Quick start

```go
// hello.zgo
package main

import "dev"

func main() {
	dev.Printf("hello from ZGo: %d\n", 42)
}
```

```
make && make zgoc                     # zForth and the compiler (build/zgoc)
build/zgoc run hello.zgo              # compile and run on the Linux zforth
build/zgoc build -o hello.zf hello.zgo   # Forth source + hello.json manifest
build/zgoc image -o build/hello hello.zgo  # standalone binary, program in ROM
```

Source files use the `.zgo` extension. `examples/` has a program for every
area of the language; `examples/thermostat` is a complete device script.

## 2. Programs and entry points

- A program is `package main`, in one or more `.zgo` files compiled together.
- The only import is `"dev"` (section 9).
- **Nothing runs by itself.** Loading a program only defines its words and
  sets constant initial values. The firmware then calls:
  - **`z-init`**: initializes package variables whose initial values are not
    constants (in Go's dependency order), then runs every `func init()` in
    source order. Call it once after loading or mounting a program. It
    always exists.
  - **`z-main`**: `func main()`, if the firmware wants it. Always exists
    (empty if there is no `main`). `zgoc run` and the example binaries call
    `z-init` then `z-main`.
  - **Exported functions** (capitalized names, like `OnTemperature`):
    entry points the firmware calls by name, as the word `z-OnTemperature`.
- **Exported package variables** (capitalized) are public too: the word
  `z-Name` pushes the variable's address, so the firmware can read or set it.
- Everything else is internal and named `z.name` (functions, variables),
  `z.Type.method` (methods) or `z:...` (compiler-made words). Don't call
  these from outside.

**The manifest.** `zgoc build` writes a JSON manifest next to the Forth:

```json
{
  "init": "z-init",
  "main": "z-main",
  "exports": [
    { "name": "OnTemperature", "word": "z-OnTemperature",
      "params": [ { "name": "t", "type": "float32", "cells": 1 },
                  { "name": "ts", "type": "uint64", "cells": 2 } ],
      "results": [] }
  ],
  "vars": [ { "name": "Presses", "word": "z-Presses", "type": "[4]uint16", "size": 8 } ]
}
```

To call an export, push the arguments in order (a two-cell value as low cell
then high cell), run the word, then pop the results (the last result is on
top). From C: `zf_push()` each argument, `zf_eval(ctx, "z-OnTemperature")`,
then `zf_pop()` the results; or `zf_eval(ctx, "21.5e0 1700000000000. z-OnTemperature")`.

Several programs can be loaded one after another (say, base firmware in ROM
and a downloaded script). Each defines its own `z-init` and `z-main`; the
name finds the most recently loaded one, so call `z-init` right after
loading each program. Compiled code is not affected: Forth binds words when
code is compiled.

## 3. Types

| Type | Stack cells | Bytes in memory | Notes |
| --- | --- | --- | --- |
| `bool` | 1 | 1 | true is -1, false is 0 |
| `int8`, `int16` | 1 | 1, 2 | re-narrowed after arithmetic |
| `uint8`/`byte`, `uint16` | 1 | 1, 2 | re-narrowed after arithmetic |
| `int32`/`rune`, **`int`** | 1 | 4 | **`int` is 32-bit**, as in TinyGo |
| `uint32`, **`uint`**, `uintptr` | 1 | 4 | `uint` is 32-bit |
| `int64`, `uint64` | 2 | 8 | two cells `( lo hi )` |
| `float32` | 1 | 4 | IEEE single; hardware on most targets |
| `float64` | 2 | 8 | IEEE double; **software on the ESP32-C3** |
| `string` | 2 | 8 | `( addr len )`, read-only |
| `[N]T` | address | N × size | fixed arrays |
| `[]T` | 2 | 8 | `( addr len )` view of an array; **no capacity** |
| `struct` | address | fields, aligned to ≤ 4 | |
| `*T` | 1 | 4 | pointers to variables, elements and fields |
| named types (`type Celsius float32`) | as the underlying type | | methods allowed |

Untyped constants follow Go: `x := 1.5` is a `float64`, `n := 7` is an `int`.
In memory, two-cell values are stored low cell first (the native layout on
the little-endian ESP32), and struct fields are laid out in order with
natural alignment up to 4 bytes, so C code on the device can read them.

## 4. Declarations and storage

There is no heap and no garbage collector. Everything has a fixed place:

- **Package variables** live in one static data block (in the RAM data
  window when the program is baked into ROM, so they stay writable).
  - A constant initial value, or a composite literal made only of constants
    (`var table = [3]int32{1, 2, 3}`), is stored when the program is loaded,
    so it is part of a ROM image.
  - Any other initializer (`var scale = compute()`) runs in `z-init`, in Go's
    dependency order, before the `init` functions.
  - Package-level slice and pointer literals work too:
    `var primes = []uint16{2, 3, 5}`, `var cfg = &Config{Rate: 10}`.
- **Local scalar variables** (numbers, bools, strings, slices, pointers) live
  in the function's frame on the return stack.
- Taking the address of a local scalar (`&x`) is not supported: frame slots
  have no address. Local arrays and structs (below) are addressable.
- **Local arrays and structs**, and **composite literals inside functions**
  (`p := Point{1, 2}`, `f([]int32{1, 2})`), get static storage, one area per
  declaration or literal. They are zeroed or filled each time the
  declaration or literal is executed, so they behave like fresh values, with
  two consequences (section 12):
  - they are not allowed in recursive functions (a compile error);
  - a slice or pointer to one stays valid after the function returns but
    points at storage the next call reuses.
- **Constants** (`const`, `iota`) are folded at compile time.
- Type declarations must be at package level.

## 5. Functions and methods

- Parameters and results can be any type except arrays and structs by value:
  - **pass arrays as slices**: `sum(buf[:])`;
  - **struct parameters** are allowed but **read-only** (they are passed by
    address); to modify a struct, pass a pointer `*T`;
  - a function cannot return an array or a struct; return a pointer, or fill
    the caller's value through a pointer or slice.
- Multiple results, named results and bare `return` work as in Go, as does
  `f(g())` when `g` returns several values.
- Recursion and mutual recursion work, in any declaration order. Depth is
  limited by the return stack (section 14).
- **Methods** on named types:
  - pointer receivers (`func (s *Sensor) Read()`) work as in Go;
  - value receivers of struct types are passed by address and are
    **read-only**, like struct parameters (modifying one is a compile error);
    value receivers of other types (`func (c Celsius) F() float32`) are
    ordinary copies;
  - the method must be called (`s.Read()`), not used as a value.
- Not supported: variadic functions (only `dev.Printf` and `dev.Format` take
  `...`), function values, closures, method values, generics.
- **Host bindings**: a function without a body calls a zForth word named by
  a `//zf:forth` comment. The text after `//zf:forth` is emitted in place of
  the call, after the arguments are pushed, so it can be any Forth:

  ```go
  //zf:forth led-set
  func LedSet(on bool)

  //zf:forth adc-read
  func adcRead(channel uint8) uint16

  //zf:forth + 2 *
  func sumTwice(a, b int32) int32
  ```

  The word must take the arguments as pushed (in order, two-cell values as
  `lo hi`) and leave the results. For Linux runs, put stand-ins in a
  `host.zf` next to the program; `zgoc` loads it automatically.

## 6. Statements

Supported:

- Assignment `=`, `:=`, every `op=`, `++`, `--`, multiple assignment
  (`a, b = b, a`) with Go's evaluation order.
- `var` (zero-initialized), `const`.
- `if` / `else if` / `else`, with an init statement.
- `for` in every form: three-clause, condition only, infinite,
  `for i := range n` over an integer, `for i, v := range` an array, slice or
  string. Go 1.22 loop variable rules apply (changing `i` inside
  `for i := range n` does not change the iteration count).
- `switch` with or without a tag and an init statement, `case a, b:`,
  `default` anywhere, `fallthrough`. Cases are evaluated lazily, in order.
- `break` (leaves the innermost `for` or `switch`), `continue`, labeled
  `break` and `continue` on `for` and `switch` statements.
- `return`.

Not supported: `goto`, `go`, `defer`, `select`, channel send, labels on other
statements, local type declarations.

## 7. Expressions and operators

- Arithmetic `+ - * / %`, bitwise `& | ^ &^ << >>`, unary `- + ^ !`,
  comparisons, `&&` and `||` (short-circuit), `&x` and `*p`, indexing,
  slicing, field selection (with automatic dereference of pointers).
- **Integer semantics match Go**: overflow wraps around, division truncates
  toward zero, `MinInt32 / -1 == MinInt32`, `x % -1 == 0`, shifts by the full
  width or more give 0 (or -1 for `>>` of a negative signed value). 8- and
  16-bit types wrap at their own width. Division by zero aborts (section 11).
- **Conversions** between all numeric types, as in Go. Float to integer
  truncates toward zero; out-of-range values saturate (Go leaves the result
  unspecified). `string(b)` of a `[]byte` is supported (a view, section 8).
- Comparisons: numbers, bools, strings (`==`, `<`, ... byte by byte),
  pointers, and slices against `nil`. Not arrays or structs.
- Builtins:

  | Builtin | Notes |
  | --- | --- |
  | `len(x)` | arrays (constant), slices, strings |
  | `cap(a)` | arrays only (slices have no capacity) |
  | `min(...)`, `max(...)` | numbers, any number of arguments |
  | `copy(dst, src)` | slices, or a string into a byte slice; overlapping is fine; returns the count |
  | `print(...)`, `println(...)` | print values with `%v` formatting (to the console) |
  | `panic(v)` | prints `panic: v` and aborts the call (section 11) |

  `append`, `make`, `new`, `delete`, `close`, `clear`, `complex`, `real`,
  `imag` and `recover` are rejected.
- **Index checks**: array, slice and string indexes and slice bounds are
  checked; an out-of-range index aborts the call, as a Go panic would.
  `zgoc -nobounds` drops the checks for trusted code. Constant indexes into
  arrays are checked at compile time and cost nothing at run time.

## 8. Strings

- String literals are compiled byte for byte into the program (read-only,
  and in ROM in an image), so UTF-8 and every Go escape (`\n \t \" \\ \xHH
  é` ...) work.
- `len(s)` is the length in bytes; `s[i]` is a byte; `s[a:b]` is a substring
  (no copy); `==`, `!=`, `<` ... compare bytes.
- `for i, r := range s` decodes UTF-8 exactly as Go does: `i` is the byte
  offset, `r` the rune, and invalid bytes give `U+FFFD`.
- **No concatenation**: `s1 + s2` would allocate. Build strings with
  `dev.Format` into a byte array (constant expressions like `"a" + "b"` are
  fine).
- `string(buf[:n])` makes a string from bytes **without copying**: the string
  is a view of the buffer and changes if the buffer changes. Copy the bytes
  to another array first if you need to keep them.
- `[]byte(s)`, `string(rune)` and `string(int)` would allocate and are
  rejected. Use `copy(buf[:], s)` to get the bytes of a string.

## 9. The dev package

`import "dev"` gives the device API, the same on every BlueStreak device:

| Function | Description |
| --- | --- |
| `dev.Printf(format string, args ...any)` | print formatted output (section 10) |
| `dev.Format(buf []byte, format string, args ...any) int` | format into `buf`, like `fmt.Sprintf` without allocating; returns the bytes written; output that does not fit is cut off |
| `dev.Print(s string)` | print a string as is |
| `dev.Delay(ms uint32)` | wait `ms` milliseconds |
| `dev.Millis() uint64` | milliseconds since the device started |

Device-specific functions (pins, sensors, radios, publishing) are bound with
`//zf:forth` (section 5) to words the firmware provides.

## 10. Formatting

`dev.Printf` and `dev.Format` take Go format strings, which **must be
constants**. The compiler checks each verb against its argument's type and
rewrites it for zForth's `fmt`, keeping flags (`+ - # 0` and space), width
and precision.

| Verb | Arguments |
| --- | --- |
| `%d` | integers |
| `%x` `%X` | integers |
| `%c` | integers (a byte) |
| `%f` `%e` `%E` `%g` `%G` | `float32`, `float64` (`%F` is `%f`) |
| `%s` | `string`, `[]byte` |
| `%t` | `bool` |
| `%v` | integers (`%d`), floats (`%g`), strings and byte slices (`%s`), bools (`%t`) |
| `%%` | a literal `%` |

Rejected: `%q %T %p %b %o %O %U %w`, argument indexes (`%[1]d`), `*` widths,
verbs that don't match the argument type, and missing or extra arguments.
One call can take at most 16 argument cells (a two-cell value counts twice;
strings and bools count two): split longer output into several calls.

Output differs from Go's `fmt` in a few cases (section 12).

## 11. Runtime errors

These abort the current call, meaning the `zf_eval()` the firmware made. The
stacks are reset, `zf_eval()` returns an error code, the program's variables
keep their values, and the next call works normally:

| Error | zForth result |
| --- | --- |
| index or slice bounds out of range | `ZF_ABORT_BOUNDS` ("index out of range") |
| integer division or remainder by zero | `ZF_ABORT_DIVISION_BY_ZERO` |
| `panic(v)`: prints `panic: v` first | `ZF_ABORT_USER` ("aborted") |
| too deep recursion or too many locals | `ZF_ABORT_RSTACK_OVERRUN` |
| too deep expression evaluation | `ZF_ABORT_DSTACK_OVERRUN` |

There is no `recover` and there are no deferred calls. **Nil pointers are not
checked**: dereferencing one reads or writes address 0 of the dictionary.
Check pointers yourself where they can be nil.

## 12. Differences from Go

Everything ZGo accepts behaves as in Go except:

1. **`int` and `uint` are 32-bit** (`uintptr` too). Use `int64` where values
   can exceed ±2³¹, for example millisecond timestamps.
2. **Slices have no capacity.** `cap()` of a slice and three-index slices
   `a[i:j:k]` are rejected, and re-slicing past a slice's length aborts (Go
   allows it up to the capacity).
3. **`string(byteSlice)` does not copy**: the string is a view of the bytes.
4. **Local arrays and structs and composite literals in functions use static
   storage** (section 4): not allowed in recursive functions, and a slice or
   pointer to one that outlives the call shares storage with the next call.
5. **Struct parameters and struct value receivers are read-only** (passed by
   address). If the function also changes the original through another
   path, it sees the change.
6. **`range` over an array does not copy the array first**: changes to later
   elements during the loop are seen by later iterations.
7. **Formatting uses C `printf` underneath**:
   - `%v` and `%g` on floats print like C's `%g` (6 significant digits,
     `0.1` but `3.14159` for π), not Go's shortest exact form; use an explicit
     precision such as `%.3f` when it matters;
   - `%x` of a negative signed integer prints its two's-complement bits
     (`ffffffff`), where Go prints `-1`;
   - `%c` prints one byte, so runes above 255 do not print as UTF-8;
   - `%s` with a precision cuts at that many bytes, not runes;
   - `print` and `println` write to the console (Go's go to stderr) and
     format floats with `%g`.
8. **Negative shift counts** give 0 (or -1 for `>>` of a negative value)
   instead of panicking.
9. **Float to integer conversions out of range saturate**; NaN converts to 0.
10. **`min` and `max` of floats with a NaN argument** do not necessarily
    return NaN.
11. **Runtime errors abort the firmware's call** instead of crashing the
    program (section 11); `panic` prints `panic: v` without a stack trace.
12. **Nil pointer dereferences are not detected.**
13. **Package initialization**: constant initializers run at load time (and
    are baked into a ROM image); the rest, and `init` functions, run when the
    firmware calls `z-init`. `main` runs only if the firmware calls `z-main`.
14. **Exported names** can be at most 29 characters (zForth words are at
    most 31 bytes, including the `z-` prefix).
15. **Recursion depth is limited** by the device's return stack (section 14).

## 13. Not supported

| Construct | Message / what to use instead |
| --- | --- |
| `go`, channels, `select` | goroutines and channels are not supported |
| `append`, `make`, `new` | no dynamic allocation: use a fixed array (`var buf [64]byte`) |
| maps | maps are not supported: use arrays (e.g. a small table you search) |
| `s1 + s2`, `s += t` on strings | string concatenation allocates: use `dev.Format` into a byte array |
| `[]byte(s)`, `string(r)` for a rune or int | would allocate: `copy(buf[:], s)`; format runes with `%c` |
| `import "fmt"` (or any package but `dev`) | use `dev.Printf` / `dev.Format` |
| `defer`, `recover` | not supported (`panic` is) |
| interfaces (including `error`, `any` outside `dev`), type assertions, type switches | return status values (a `bool` or an `int` code) instead of `error` |
| generics | not supported |
| closures, function values, method values | call functions directly; use a `switch` to choose |
| `goto`, labels on statements other than `for`/`switch` | use loops with labeled `break`/`continue` |
| local arrays/structs in recursive functions (also composite literals, and arrays or structs swapped in a multiple assignment) | declare them at package level |
| `&x` of a local scalar variable, or calling a pointer method on one (`var n Counter; n.Inc()`) | locals live in the call frame, which has no address: use a package-level variable, or a local struct or array |
| arrays passed by value | pass a slice: `f(a[:])` |
| array or struct results | return a pointer, or fill the caller's value through a pointer or slice |
| comparing arrays or structs with `==` | compare elements or fields |
| `cap` of a slice, three-index slices | slices have no capacity; use `len` |
| variadic functions | pass a slice |
| embedded struct fields | name the field |
| local type declarations | declare types at package level |
| `complex64`, `complex128`, `unsafe` | not supported |
| non-constant format strings | format strings must be constants |
| unsupported format verbs | the message lists the supported verbs |
| a function without a body and no `//zf:forth` | bind it to a zForth word |

## 14. Device limits

These are zForth configuration values (`src/zfconf_common.h`); a device
build can change them.

| Limit | Default | What uses it |
| --- | --- | --- |
| Return stack, `ZF_RSTACK_SIZE` | 128 cells | each active call: frame slots + 3 cells. Recursion depth and nesting of calls with many locals |
| Data stack, `ZF_DSTACK_SIZE` | 128 cells | expression evaluation and arguments |
| Writable dictionary, `ZF_DICT_MAX_SIZE` | 32 KB | programs loaded as source, plus every program's data block |
| `fmt` arguments per call | 16 cells | `dev.Printf`, `dev.Format`, `print`, `println`, `panic` |
| Word names | 31 bytes | exported names at most 29 characters |
| Nesting | 32 open loops, switches and `fallthrough` case labels per function | compile error beyond that |

Loading `forth/zgo.zf` takes 128 bytes of RAM for the compiler's label table
(used only while compiling). A frame slot is one cell (4 bytes). `zgoc build`
output shows each function's frame size in its `frame` or `locals` prologue.

## 15. Style guide for device code

- **Prefer `float32`.** The ESP32-C3 has no FPU; `float64` is about 1.5 to 2
  times slower than `float32`. Remember untyped float constants are
  `float64`: write `var t float32 = 21.5` or `float32(21.5)`.
- **Use `int`/`int32` for counters and indexes**, `int64`/`uint64` only where
  values need it (timestamps from `dev.Millis()`); 64-bit arithmetic takes
  two cells and more work.
- **Declare buffers at package level** (`var msg [128]byte`) and reuse them.
  Format messages with `dev.Format(msg[:], ...)`.
- **Keep functions small** and avoid deep recursion: frames live on a small
  return stack.
- **Pass structs by pointer** to anything that changes them; use pointer
  receivers for methods that change state.
- **Export only entry points** the firmware calls; keep helpers lowercase.
  Document each export's units (`ts` in milliseconds, `t` in °C).
- **Return status values instead of errors**: `func Set(v float32) bool`.
- **Check pointers that can be nil** before using them.
- **Keep `init` cheap**; put work the firmware should trigger in exported
  functions.

## 16. Tools

```
zgoc build [-o out.zf] [-manifest out.json] file.zgo...
zgoc run   [-host host.zf] [-calls calls.txt] [-i] file.zgo...
zgoc image -o dir [-san none|asan|ubsan] file.zgo...
zgoc test  [-san ...] [-update] dir...
```

- **build**: write the Forth and the manifest.
- **run**: compile and run on the Linux zforth (`src/linux/zforth`, built
  with `make`): load `bs.zf`, `zgo.zf`, `host.zf` and the program, call
  `z-init` and `z-main`, then run the Forth lines in `calls.txt` to play the
  firmware (for example `21.5e0 1700000000000. z-OnTemperature`). `host.zf`
  and `calls.txt` next to the first source file are used automatically.
- **image**: build a standalone Linux binary whose ROM image holds `bs.zf`,
  `zgo.zf`, `host.zf` and the program, mounted exactly as on a device. Run it
  with `-e z-init -e z-main`, then a calls file or Forth on stdin.
- **test**: for each example directory (`main.zgo` and `expected.txt`), check
  that the committed `main.zf` and `main.json` are up to date and that run
  mode and image mode both print `expected.txt`; for each error directory,
  check that every line marked `// ERROR "text"` gets that error and no other
  line gets one. `-update` rewrites the expected files.
- Compile flags: `-nobounds` drops index checks, `-comments` adds source
  positions to the Forth, `-O0` turns off the peephole optimizer.

`make zgoc` builds the compiler, `make examples` builds every example as a
binary in `build/examples/<name>/<name>`, and `make test` runs all zForth and
ZGo tests: Go unit tests, programs compared against real Go
(`zgo/testdata/compat`), the examples in both modes (and again under UBSan)
and the error tests. `zgo/internal/gorun/gorun.sh dir` runs an example with
the real Go toolchain, which is handy for checking that ZGo prints what Go
does.
