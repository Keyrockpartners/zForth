# zForth syscall requirements for IoT devices

This document lists the host syscalls a BlueStreak IoT device implementation is expected to provide.

## Required core zForth syscalls

These are defined by `zf_syscall_id` in `src/zforth/zforth.h` and are used by the core Forth library.

| ID | Name | Forth word | Stack effect | Required behavior |
| --- | --- | --- | --- | --- |
| `0` | `ZF_SYSCALL_EMIT` | `emit` | `( char -- )` | Output one character. |
| `1` | `ZF_SYSCALL_PRINT` | `.` | `( n -- )` | Output one cell as a number. |
| `2` | `ZF_SYSCALL_TELL` | `tell` | `( addr len -- )` | Output `len` bytes from zForth dictionary memory at `addr`. |

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
