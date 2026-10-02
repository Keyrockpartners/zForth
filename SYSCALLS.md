# zForth syscall requirements for IoT devices

This document lists the host syscalls a BlueStreak IoT device implementation is expected to provide.

## Required core zForth syscalls

These are defined by `zf_syscall_id` in `src/zforth/zforth.h` and are used by the core Forth library.

| ID | Name | Forth word | Stack effect | Required behavior |
| --- | --- | --- | --- | --- |
| `0` | `ZF_SYSCALL_EMIT` | `emit` | `( char -- )` | Output one character. |
| `1` | `ZF_SYSCALL_PRINT` | `.` | `( n -- )` | Output one cell as a signed integer (`ZF_CELL_FMT`) followed by a space. |
| `2` | `ZF_SYSCALL_TELL` | `tell` | `( addr len -- )` | Output `len` bytes from zForth dictionary memory at `addr`. |

Syscalls that take an `( addr len )` pair must resolve it with `zf_dict_addr(ctx, addr, len)`, never by indexing `ctx->dict` directly. `zf_dict_addr()` checks the range (aborting on failure) and handles all three regions an address can fall in: the read-only ROM image, the writable RAM dictionary, and the data window at `ZF_DATA_ADDR` where variables compiled into a prebuilt dictionary live.

The pointer from `zf_dict_addr()` is only valid until the dictionary is next written. The dictionary can grow and move on any write, including `zf_dict_write_bytes()` and `zf_eval()`. Copy the bytes out, or call `zf_dict_addr()` again after writing, rather than holding the pointer.

Device builds should call `zf_init_checked()` (not `zf_init()`) and check the result of `zf_dict_mount_rom()`. Both return `ZF_ABORT_OUTSIDE_MEM` on allocation failure instead of aborting. The writable dictionary is capped at `ZF_DICT_MAX_SIZE` bytes (default 32 KB, not counting a mounted ROM image), which can be changed at runtime with `zf_dict_set_limit()`. A script that hits the cap aborts with `outside memory`; the rest of the firmware's heap is unaffected.

## Required host callbacks

Besides `zf_host_sys()`, which dispatches the syscalls in this document, the host must provide:

| Function | Required | Behavior |
| --- | --- | --- |
| `zf_cell zf_host_parse_num(zf_ctx *ctx, const char *buf)` | Yes | Called by `zf_eval()` for every token that is not a known word, both when interpreting (the number is pushed) and when compiling (it is compiled as a literal). Parse the whole token as a number and return it, or call `zf_abort(ctx, ZF_ABORT_NOT_A_WORD)` if it is not one. Not needed for the prebuilt ROM image, whose numbers were parsed at export time, but needed for any script source evaluated on the device. Implement it by calling `zf_parse_num()`, as `src/linux/main.c` does. |
| `void zf_host_trace(zf_ctx *ctx, const char *fmt, va_list va)` | Only with `ZF_ENABLE_TRACE` | Output trace text. Tracing is off by default on device builds. |

`zf_host_parse_num()` defines what number syntax scripts can use, so devices should use the shared `zf_parse_num()` to behave the same as Linux. It accepts:

- Decimal integers from `-2147483648` to `4294967295`; values above `2147483647` wrap to the same bit pattern as a negative number (`4294967295` is `-1`). A leading zero does not mean octal.
- Hex integers with a `0x` prefix, up to `0xFFFFFFFF`, with an optional sign.
- With `ZF_ENABLE_FLOAT`, floats: any non-hex token containing `.`, `e` or `E` that does not end in `.`, such as `1.5`, `-2.0`, `.5`, `5.0` or `1e3`, parsed with `strtof`.

A token ending in `.` with only digits (and an optional sign) before it, such as `123.`, `-5.` or `0xFF.`, is a double-cell literal (standard Forth syntax), from -2^63 to 2^64-1. With `ZF_ENABLE_DFLOAT`, a decimal number with a `d` exponent in place of `e`, such as `1.5d0`, `6.02d23` or `-2.25d-3`, is a double-precision float literal. The interpreter handles both of these itself before calling `zf_host_parse_num()`, since they produce two cells, so hosts need no support for them.

## Cells, integers and floats

A cell (`zf_cell`) is an `int32_t`. Integer arithmetic wraps around on overflow; `/` and `%` truncate toward zero, and dividing by zero aborts. Unsigned operations are separate words: `u< u> u<= u>= u/ umod rshift umin umax u.`.

With `ZF_ENABLE_DOUBLE_CELL` (on by default), 64-bit integers are double cells: two stack entries, the low cell below the high cell, written as literals with a final dot (`1700000000000.`, `-5.`). As with single cells, signed and unsigned share most words (`d+ d- ud* d= 2@ 2!`) and differ only where they must: `d< d/ dmod m* d.` are signed, `du< ud/ udmod um* ud.` unsigned. `forth/double.zf` lists them all; none need host support beyond `fmt`.

With `ZF_ENABLE_DFLOAT` (on by default), a double-precision float is an IEEE-754 double in two cells, `( lo hi )`, written with a `d` exponent (`1.5d0`). Its words (`df+ df- df* df/ df< df= s>df df>s d>df df>d f>df df>f dfsqrt` ... and those in `forth/dfloat.zf`) are built in. A syscall that takes or returns one should convert with `zf_cells_to_dfloat()` and `zf_dfloat_to_cells()`. `df!` stores the low cell first, which on the little-endian ESP32 family matches a C `double` in memory.

With `ZF_ENABLE_FLOAT` (on by default), a float is an IEEE-754 single-precision value stored in one cell as its bit pattern, on the same stack as integers. The float words (`f+ f- f* f/ f< f= s>f u>f f>s fsqrt ffloor fceil fround ftrunc`, and those in `forth/float.zf`) are built into the interpreter, so hosts implement nothing for them. A syscall that takes or returns a float should convert with `zf_cell_to_float()` and `zf_float_to_cell()`.

## Prebuilt dictionaries

`zforth -H NAME file.zf...` emits `NAME[]`, `NAME_len`, and `NAME_data_len`. The array is the dictionary image followed by the initial contents of the data window (its last `NAME_data_len` bytes). Pass all three unchanged to `zf_dict_mount_rom()` (ROM builds) or `zf_dict_import_with_data()`.

Variables defined with `var`/`variable`, buffers made with `N buffer: name`, and other storage from `data-here`/`data-allot` get their storage in the data window during export. The window is copied to RAM at mount time, so they stay writable when the image is in flash. Anything else written into the dictionary at export time — e.g. a buffer made with `here N allot` — is part of the read-only image and aborts with `outside memory` if written at runtime.

A checkpoint compiled into the image can be used at runtime to drop everything defined since mount; new definitions then start right after the data window. `bs.zf` ends with `chkpt empty`, so running `empty` resets the interpreter to the baked-in words (keeping their variables' values) and clears the stacks.

## Required BlueStreak user syscalls

Application-specific syscalls start at `ZF_SYSCALL_USER` (`128`). For IoT device implementations, only `ZF_SYSCALL_USER + 4` and greater need to be implemented. `ZF_SYSCALL_USER + 0` through `ZF_SYSCALL_USER + 3` are Linux-only development helpers.

| ID | Name / Forth word | Stack effect | Required behavior |
| --- | --- | --- | --- |
| `ZF_SYSCALL_USER + 4` (`132`) | `fmt` | `( arg... fmt-addr fmt-len -- )` | Format and output a string from dictionary memory. Supported format sequences are `%%`, `%d` and `%n` (signed integer), `%u` (unsigned integer), `%f` (float, formatted like printf `%g`), `%D` and `%U` (signed and unsigned 64-bit double cell), `%F` (double-precision float, formatted like printf `%.15g`), `%c`, and `%s`. Format arguments are pushed before the format string, in left-to-right placeholder order. A `%s` argument is `( addr len )` and a `%D`/`%U`/`%F` argument is `( lo hi )`; every other argument is one cell. `u.`, `f.`, `d.`, `ud.` and `df.` are built on `fmt`. |

IDs `133`–`136` (formerly `floor`, `ceil`, `round`, `trunc`) are no longer used; those operations are now the built-in float words `ffloor`, `fceil`, `fround` and `ftrunc`.

## Linux-only user syscalls

IoT device implementations do not need to implement these development-only syscalls:

| ID | Forth word | Linux behavior |
| --- | --- | --- |
| `ZF_SYSCALL_USER + 0` (`128`) | `quit` | Exit the Linux process. |
| `ZF_SYSCALL_USER + 1` (`129`) | `sin` | Calculate the sine of a float. |
| `ZF_SYSCALL_USER + 2` (`130`) | `include` | Include another source file. |
| `ZF_SYSCALL_USER + 3` (`131`) | `save` | Save the Linux dictionary image. |
