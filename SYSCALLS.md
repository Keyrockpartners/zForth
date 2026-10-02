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

The pointer from `zf_dict_addr()` is only valid until the dictionary is next written. The dictionary can grow and move on any write, including `zf_dict_write_bytes()` and `zf_eval()`. Copy the bytes out, or call `zf_dict_addr()` again after writing, rather than holding the pointer.

Device builds should call `zf_init_checked()` (not `zf_init()`) and check the result of `zf_dict_mount_rom()`. Both return `ZF_ABORT_OUTSIDE_MEM` on allocation failure instead of aborting. The writable dictionary is capped at `ZF_DICT_MAX_SIZE` bytes (default 32 KB, not counting a mounted ROM image), which can be changed at runtime with `zf_dict_set_limit()`. A script that hits the cap aborts with `outside memory`; the rest of the firmware's heap is unaffected.

## Prebuilt dictionaries

`zforth -H NAME file.zf...` emits `NAME[]`, `NAME_len`, and `NAME_data_len`. The array is the dictionary image followed by the initial contents of the data window (its last `NAME_data_len` bytes). Pass all three unchanged to `zf_dict_mount_rom()` (ROM builds) or `zf_dict_import_with_data()`.

Variables defined with `var`/`variable`, buffers made with `N buffer: name`, and other storage from `data-here`/`data-allot` get their storage in the data window during export. The window is copied to RAM at mount time, so they stay writable when the image is in flash. Anything else written into the dictionary at export time — e.g. a buffer made with `here N allot` — is part of the read-only image and aborts with `outside memory` if written at runtime.

A checkpoint compiled into the image can be used at runtime to drop everything defined since mount; new definitions then start right after the data window. `bs.zf` ends with `chkpt empty`, so running `empty` resets the interpreter to the baked-in words (keeping their variables' values) and clears the stacks.

## Required BlueStreak user syscalls

Application-specific syscalls start at `ZF_SYSCALL_USER` (`128`). For IoT device implementations, only `ZF_SYSCALL_USER + 4` and greater need to be implemented. `ZF_SYSCALL_USER + 0` through `ZF_SYSCALL_USER + 3` are Linux-only development helpers.

| ID | Name / Forth word | Stack effect | Required behavior |
| --- | --- | --- | --- |
| `ZF_SYSCALL_USER + 4` (`132`) | `fmt` | `( arg... fmt-addr fmt-len -- )` | Format and output a string from dictionary memory. Supported format sequences are `%%`, `%d`, `%n`, `%c`, and `%s`. Format arguments are pushed before the format string, in left-to-right placeholder order. A `%s` argument is `( addr len )`. |
| `ZF_SYSCALL_USER + 5` (`133`) | `floor` | `( n -- n )` | Push the floor of the input value. |
| `ZF_SYSCALL_USER + 6` (`134`) | `ceil` | `( n -- n )` | Push the ceiling of the input value. |
| `ZF_SYSCALL_USER + 7` (`135`) | `round` | `( n -- n )` | Push the rounded input value. |
| `ZF_SYSCALL_USER + 8` (`136`) | `trunc` | `( n -- n )` | Push the truncated input value. |

## Linux-only user syscalls

IoT device implementations do not need to implement these development-only syscalls:

| ID | Forth word | Linux behavior |
| --- | --- | --- |
| `ZF_SYSCALL_USER + 0` (`128`) | `quit` | Exit the Linux process. |
| `ZF_SYSCALL_USER + 1` (`129`) | `sin` | Calculate sine. |
| `ZF_SYSCALL_USER + 2` (`130`) | `include` | Include another source file. |
| `ZF_SYSCALL_USER + 3` (`131`) | `save` | Save the Linux dictionary image. |
