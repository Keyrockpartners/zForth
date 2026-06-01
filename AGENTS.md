# AGENTS.md

This directory contains a Forth implementation that we plan to use for scripting on IoT devices that connect to our IoT backend platform.

## Project notes

- The Linux implementation is primarily for testing and development.
- The ATmega8 implementation is currently unused; it does not need to be kept up to date unless we explicitly decide to use it again.
- Any new Forth commands we add should go in `bs.zf`.
- If a new set of Forth commands is large enough or separate enough to deserve its own file, create a new `xx.zf` file instead of adding it to `bs.zf`.
