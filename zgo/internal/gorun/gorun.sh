#!/usr/bin/env bash
# gorun.sh <example dir>: run the example's .zgo files with real Go.
set -e
src=$(cd "$1" && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/dev"
cat > "$tmp/go.mod" <<'MOD'
module ex
go 1.22
MOD
cat > "$tmp/dev/dev.go" <<'DEV'
package dev

import (
	"fmt"
	"time"
)

var start = time.Now()

func Printf(format string, args ...any) { fmt.Printf(format, args...) }
func Format(buf []byte, format string, args ...any) int {
	return copy(buf, fmt.Sprintf(format, args...))
}
func Print(s string)     { fmt.Print(s) }
func Delay(ms uint32)    { time.Sleep(time.Duration(ms) * time.Millisecond) }
func Millis() uint64     { return uint64(time.Since(start).Milliseconds()) }
DEV
for f in "$src"/*.zgo; do
	sed 's#import "dev"#import "ex/dev"#' "$f" > "$tmp/$(basename "${f%.zgo}").go"
done
cd "$tmp" && go run . 2>&1
