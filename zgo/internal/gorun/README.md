# gorun

`gorun.sh dir` runs a ZGo example under the real Go toolchain, with a
stand-in `dev` package built on `fmt`, so ZGo's output can be compared with
Go's. Go's `int` is 64-bit on the build machine, so examples meant for this
comparison should not depend on `int` overflow.
