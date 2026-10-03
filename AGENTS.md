# AGENTS.md

This repository is the zForth engine used by BlueStreak IoT devices: a fork of
upstream zForth (`zevv/zForth`) grown into a 32-bit engine with floats,
64-bit integers, local frames and ROM images. The device platform built on it,
including the **ZGo** language and compiler, lives in
`Keyrockpartners/bs-zgo-device`, which includes this repository as a git
submodule.

**Keep this repository generic.** It holds only what any zForth user could
want: the interpreter, its Forth word files, the Linux reference host and the
engine tests. BlueStreak- and ZGo-specific code (language runtime words,
platform syscalls, protocol support) belongs in `bs-zgo-device`.

## Start here

- `SYSCALLS.md`: what a host must implement (syscalls, callbacks, abort reasons, `fmt` placeholders, prebuilt ROM dictionaries).
- `make test`: runs every engine test (`tests/run.sh`). Keep it passing, and add tests under `tests/` with every change.

## Development environment

- The engine builds with the system C compiler and `make` (`src/linux/Makefile` takes `$(CC)` from the environment, else `gcc`); on Linux the REPL also needs readline. There is no dev shell here: inside `bs-zgo-device`, its nix dev shell applies to this submodule too. On macOS, use Xcode's compiler (nixpkgs' clang produces AddressSanitizer binaries that hang on macOS).
- The Linux host is the reference host: `src/linux/main.c` plus reusable pieces declared in `src/linux/host.h` (`repl.c` the command line and REPL, `export.c` the `-H` ROM exporter, `sys.c` the core and Linux-only syscalls, `fmt.c` `fmt` and `fmt-buf`). A program that needs more syscalls (such as `bs-zgo-device`'s Linux sim host) links the pieces with its own `main()` and passes its syscalls in `zfl_config`, and can pass a poll function the REPL calls while it waits for input (to run handlers or service I/O); add generic host features to the pieces, not to `main.c`.
- `make` builds the Linux REPL (`src/linux/zforth`); `make linux-rom` builds `src/linux/zforth-rom`, with `ext.zf` baked in as a ROM image. Run `src/linux/zforth forth/ext.zf` for a REPL with the full word set (from the repo root: `ext.zf` includes its files by relative path).

## Project notes

- The Linux implementation is primarily for testing and development.
- For syscall requirements, see `SYSCALLS.md`. In the `ZF_SYSCALL_USER` range, only `(ZF_SYSCALL_USER + 4)` or greater need to be implemented on devices; `ZF_SYSCALL_USER` syscalls below that are Linux-only.
- The ATmega8 implementation is currently unused; it does not need to be kept up to date unless we explicitly decide to use it again.
- New general-purpose Forth words go in `ext.zf`, or in a new `xx.zf` file when a set of words is large or separate enough to deserve one.
- The devices are constrained (mostly ESP32-C3, which has no FPU). Prefer cheap checks, and ask before adding runtime protection that costs memory or speed.
- Compiled code stores primitive numbers, so adding a primitive or changing a feature flag in `src/zfconf_common.h` requires regenerating ROM images; the Linux exporter and the device must use the same feature flags. Images record their configuration (`ZF_IMAGE_CONFIG` in `zforth.c`) and mounting one built differently fails with `ZF_ABORT_IMAGE_MISMATCH`; bump `ZF_IMAGE_VERSION` when primitives are reordered or the image layout changes without changing the number of primitives.

## Working conventions

- Work happens on branch `zgo-base`. `master` tracks upstream zForth.
- Changes usually arrive through `bs-zgo-device`'s submodule: merge PRs here with merge commits, not squash or rebase, so the commit the submodule points at stays on `zgo-base`.
- Ask before committing or pushing.
