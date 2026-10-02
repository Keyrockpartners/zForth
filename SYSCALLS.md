# zForth syscall requirements for IoT devices

This document lists the host syscalls a BlueStreak IoT device implementation is expected to provide.

## Required core zForth syscalls

These are defined by `zf_syscall_id` in `src/zforth/zforth.h` and are used by the core Forth library.

| ID | Name | Forth word | Stack effect | Required behavior |
| --- | --- | --- | --- | --- |
| `0` | `ZF_SYSCALL_EMIT` | `emit` | `( char -- )` | Output one character. |
| `1` | `ZF_SYSCALL_PRINT` | `.` | `( n -- )` | Output one cell as a number. |
| `2` | `ZF_SYSCALL_TELL` | `tell` | `( addr len -- )` | Output `len` bytes from zForth dictionary memory at `addr`. |

Syscalls that take an `( addr len )` pair must resolve it with `zf_dict_addr(ctx, addr, len)`, never by indexing `ctx->dict` directly. `zf_dict_addr()` checks the range (aborting on failure) and handles all three regions an address can fall in: the read-only ROM image, the writable RAM dictionary, and the data window at `ZF_DATA_ADDR` where variables compiled into a prebuilt dictionary live.

## Prebuilt dictionaries

`zforth -H NAME file.zf...` emits `NAME[]`, `NAME_len`, and `NAME_data_len`. The array is the dictionary image followed by the initial contents of the data window (its last `NAME_data_len` bytes). Pass all three unchanged to `zf_dict_mount_rom()` (ROM builds) or `zf_dict_import_with_data()`.

Variables defined with `var`/`variable`, buffers made with `N buffer: name`, and other storage from `data-here`/`data-allot` get their storage in the data window during export. The window is copied to RAM at mount time, so they stay writable when the image is in flash. Anything else written into the dictionary at export time — e.g. a buffer made with `here N allot` — is part of the read-only image and aborts with `outside memory` if written at runtime.

A checkpoint compiled into the image (e.g. `chkpt base` at the end of a script) can be used at runtime to drop everything defined since mount; new definitions then start right after the data window.

## Required BlueStreak user syscalls

Application-specific syscalls start at `ZF_SYSCALL_USER` (`128`). For IoT device implementations, only `ZF_SYSCALL_USER + 4` and greater need to be implemented. `ZF_SYSCALL_USER + 0` through `ZF_SYSCALL_USER + 3` are Linux-only development helpers.

| ID | Name / Forth word | Stack effect | Required behavior |
| --- | --- | --- | --- |
| `ZF_SYSCALL_USER + 4` (`132`) | `fmt` | `( arg... fmt-addr fmt-len -- )` | Format and output a string from dictionary memory. Supported format sequences are `%%`, `%d`, `%n`, `%c`, and `%s`. Format arguments are pushed before the format string, in left-to-right placeholder order. A `%s` argument is `( addr len )`. |
| `ZF_SYSCALL_USER + 5` (`133`) | `floor` | `( n -- n )` | Push the floor of the input value. |
| `ZF_SYSCALL_USER + 6` (`134`) | `ceil` | `( n -- n )` | Push the ceiling of the input value. |
| `ZF_SYSCALL_USER + 7` (`135`) | `round` | `( n -- n )` | Push the rounded input value. |
| `ZF_SYSCALL_USER + 8` (`136`) | `trunc` | `( n -- n )` | Push the truncated input value. |

## Optional OS-object syscalls

These syscalls are available to hosts that compile zForth with `ZFORTH_EXT_OS_OBJECTS=1`. They are generic host-object hooks; the BlueStreak Berry binding maps them to retained Berry values.

`ZFORTH_EXT_OS_OBJECTS` defaults to `0` in the shared zForth configuration. When enabled, this implementation represents external object cells as NaN-tagged 64-bit `double` values, so it is only supported with the shared configuration path where `zf_cell` is an IEEE-like 8-byte `double`. External cells are opaque to zForth arithmetic: stack movement, literals, constants, `@`, `!`, and return-stack transfer preserve them, while numeric operators abort instead of treating them as numbers.

External object cells and compiled host-object constants are interpreter-local references. They are not valid after dictionary export/import, restart, or use with another `zf_ctx`/host registry unless a host-specific serializer is added later.

| ID | Name / Forth word | Stack effect | Required behavior |
| --- | --- | --- | --- |
| `ZF_SYSCALL_OS_TO_STR` (`144`) | `->os_str` | `( addr len -- os-string )` | Copy `len` bytes from zForth dictionary memory into a host string object and push an external object cell for it. |
| `ZF_SYSCALL_OS_FROM_STR` (`145`) | `<-os_str` | `( os-string -- addr len )` | Convert a host string object to bytes, copy them into zForth-owned dictionary memory, and push the resulting Forth-native string pair. |
| `ZF_SYSCALL_OS_CALL` (`146`) | `os_call` | `( fn arg1 ... argN N -- result )` | Call a host callable with `N` converted zForth arguments and push the converted result. |
| `ZF_SYSCALL_OS_SEND` (`147`) | `os_send` | `( obj selector arg1 ... argN N -- result )` | Invoke the host method/member named by `selector` on `obj` with `N` converted arguments. A selector can be made with `s\" name\" ->os_str`. |
| `ZF_SYSCALL_OS_GC` (`148`) | `os_gc` | `( -- )` | Sweep host-object references not reachable from zForth stacks or live dictionary ownership records. |
| `ZF_SYSCALL_OS_TYPE` (`149`) | `os_type` | `( value -- type-code )` | Diagnostic helper: returns `0` for numeric cells or the external-object kind tag. |

Example:

```forth
( Berry pushes a callable on the zForth stack first. )
s" blue" ->os_str 1 os_call

( Call obj.method(123). Berry pushes obj first. )
s" method" ->os_str 123 1 os_send

( Convert a host string back to a Forth-native string for tell/fmt. )
<-os_str tell
```

` s\" ...\" ` remains Forth-native `( addr len )`; use `->os_str` only when a host string object is needed.

## Linux-only user syscalls

IoT device implementations do not need to implement these development-only syscalls:

| ID | Forth word | Linux behavior |
| --- | --- | --- |
| `ZF_SYSCALL_USER + 0` (`128`) | `quit` | Exit the Linux process. |
| `ZF_SYSCALL_USER + 1` (`129`) | `sin` | Calculate sine. |
| `ZF_SYSCALL_USER + 2` (`130`) | `include` | Include another source file. |
| `ZF_SYSCALL_USER + 3` (`131`) | `save` | Save the Linux dictionary image. |
