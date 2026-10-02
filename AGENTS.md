# AGENTS.md

This directory contains a Forth implementation that we plan to use for scripting on IoT devices that connect to our IoT backend platform. On top of it we are building **ZGo**, a Go subset that compiles to zForth, for device scripts and most of the base firmware.

## Start here

- `ZGO_PLAN.md`: the ZGo plan. Its section 0 explains the background, the decisions already made, what zForth provides today, and known gotchas. Read it before working on ZGo or on zForth features it depends on.
- `SYSCALLS.md`: what a device host must implement (syscalls, callbacks, `fmt` placeholders, prebuilt ROM dictionaries).
- `make test`: runs every zForth test (see `tests/run.sh`). Keep it passing, and add tests under `tests/` with every change.

## Development environment

- The repo has a nix flake: with direnv, run `direnv allow` once, or use `nix develop`. It provides Go, `gopls` and `make`. On macOS it deliberately provides no C compiler, so zForth builds with Xcode's compiler (nixpkgs' clang produces AddressSanitizer binaries that hang on macOS).
- `make` builds the Linux REPL (`src/linux/zforth`); `make linux-rom` builds `src/linux/zforth-rom`, with `bs.zf` baked in as a ROM image. Run `src/linux/zforth forth/bs.zf` for a REPL with the full word set.

## Project notes

- The Linux implementation is primarily for testing and development.
- For IoT device syscall requirements, see `SYSCALLS.md`. In the `ZF_SYSCALL_USER` range, only `(ZF_SYSCALL_USER + 4)` or greater need to be implemented; `ZF_SYSCALL_USER` syscalls below that are Linux-only.
- The ATmega8 implementation is currently unused; it does not need to be kept up to date unless we explicitly decide to use it again.
- Any new Forth commands we add should go in `bs.zf`.
- If a new set of Forth commands is large enough or separate enough to deserve its own file, create a new `xx.zf` file instead of adding it to `bs.zf`.
- The devices are constrained (mostly ESP32-C3, which has no FPU). Prefer cheap checks, and ask before adding runtime protection that costs memory or speed.
- Compiled code stores primitive numbers, so adding a primitive or changing a feature flag in `src/zfconf_common.h` requires regenerating ROM images; the Linux exporter and the device must use the same feature flags.

## Working conventions

- Work happens on branch `bsplatform`.
- Ask before committing or pushing.
