# AGENTS.md

This directory contains a Forth implementation that we plan to use for scripting on IoT devices that connect to our IoT backend platform. On top of it we are building **ZGo**, a Go subset that compiles to zForth, for device scripts and most of the base firmware.

## Start here

- `LANGUAGE.md`: the ZGo language reference. **Read it before writing ZGo code**: it lists what is supported, every difference from Go, what is rejected and what to use instead, device limits, and a style guide.
- `ZGO_PLAN.md`: the ZGo design. Its section 0 explains the background, the decisions already made, what zForth provides today, and known gotchas. Read it before working on the compiler (`zgo/`) or on zForth features it depends on.
- `SYSCALLS.md`: what a device host must implement (syscalls, callbacks, abort reasons, `fmt` placeholders, prebuilt ROM dictionaries).
- `make test`: runs every zForth and ZGo test (`tests/run.sh`, then `make test-zgo`). Keep it passing, and add tests with every change: under `tests/` for zForth, `zgo/testdata/compat` (compared with real Go), `zgo/testdata/errors` or `examples/` for ZGo.

## Development environment

- The repo has a nix flake: with direnv, run `direnv allow` once, or use `nix develop`. It provides Go, `gopls` and `make`. On macOS it deliberately provides no C compiler, so zForth builds with Xcode's compiler (nixpkgs' clang produces AddressSanitizer binaries that hang on macOS).
- `make` builds the Linux REPL (`src/linux/zforth`); `make linux-rom` builds `src/linux/zforth-rom`, with `bs.zf` baked in as a ROM image. Run `src/linux/zforth forth/bs.zf` for a REPL with the full word set (from the repo root: `bs.zf` includes its files by relative path).
- `make zgoc` builds the ZGo compiler as `build/zgoc`. `build/zgoc run prog.zgo` compiles and runs a program on the Linux zforth; `build/zgoc image -o build/prog prog.zgo` builds a standalone binary with the program in a ROM image; `make examples` builds every example that way.

## Project notes

- The Linux implementation is primarily for testing and development.
- For IoT device syscall requirements, see `SYSCALLS.md`. In the `ZF_SYSCALL_USER` range, only `(ZF_SYSCALL_USER + 4)` or greater need to be implemented; `ZF_SYSCALL_USER` syscalls below that are Linux-only.
- The ATmega8 implementation is currently unused; it does not need to be kept up to date unless we explicitly decide to use it again.
- Any new Forth commands we add should go in `bs.zf`; words used only by code the ZGo compiler generates go in `forth/zgo.zf`.
- If a new set of Forth commands is large enough or separate enough to deserve its own file, create a new `xx.zf` file instead of adding it to `bs.zf`.
- The devices are constrained (mostly ESP32-C3, which has no FPU). Prefer cheap checks, and ask before adding runtime protection that costs memory or speed.
- Compiled code stores primitive numbers, so adding a primitive or changing a feature flag in `src/zfconf_common.h` requires regenerating ROM images; the Linux exporter and the device must use the same feature flags.

## Working conventions

- Work happens on branch `bsplatform`.
- Ask before committing or pushing.
