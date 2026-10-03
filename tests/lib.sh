#!/usr/bin/env bash
# Shared helpers for the zForth tests; sourced by every test script.
#
# A test is   t '<forth input>' '<expected output>'
# It feeds the input to each binary in $BINS (entries separated by |), and
# compares the combined stdout+stderr with colours stripped, newlines turned
# into spaces and the ends trimmed. With T_SQUEEZE=1, runs of spaces are also
# squeezed to one (useful when errors add blank lines). Errors are printed to
# stderr before normal output, which stdout buffering makes deterministic.

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
BUILD=$ROOT/tests/build
cd "$ROOT" || exit 1

CC=${CC:-gcc}
CFLAGS_COMMON="-I$ROOT/src/linux -I$ROOT/src/zforth -g -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-result"
SAN_ASAN="-Os -fsanitize=address"
SAN_UBSAN="-O1 -fsanitize=address,undefined -fno-sanitize-recover=all"
# The Linux host's reusable pieces (src/linux/host.h) and the engine; the
# reference host adds src/linux/main.c
HOST_SRCS="src/linux/repl.c src/linux/sys.c src/linux/fmt.c src/linux/export.c src/zforth/zforth.c"

# Default binaries: the RAM build loading ext.zf, and the ROM build with ext.zf
# baked in. run.sh overrides this through ZF_TEST_BINS.
BINS=${ZF_TEST_BINS:-"$BUILD/asan/zforth forth/ext.zf|$BUILD/asan/stock/zforth-rom"}
T_SQUEEZE=${T_SQUEEZE:-0}
pass=0
fail=0

# Run a command with a time limit, so a hang fails instead of stalling
with_timeout() {
	if command -v perl >/dev/null 2>&1; then
		perl -e 'alarm 30; exec @ARGV or exit 127' "$@"
	else
		"$@"
	fi
}

t() {
	local input=$1 want=$2 bin got
	local IFS_SAVE=$IFS
	IFS='|' read -r -a bins <<< "$BINS"
	IFS=$IFS_SAVE
	for bin in "${bins[@]}"; do
		# $bin is a command line (binary plus files), word splitting intended
		# shellcheck disable=SC2086
		got=$(printf '%s\n' "$input" | with_timeout $bin 2>&1 | grep -v '^Welcome' | sed $'s/\x1b\\[[0-9;]*m//g' | tr '\n' ' ')
		[ "$T_SQUEEZE" = 1 ] && got=$(printf '%s' "$got" | tr -s ' ')
		got=$(printf '%s' "$got" | sed 's/^ *//; s/ *$//')
		if [ "$got" = "$want" ]; then
			pass=$((pass + 1))
		else
			fail=$((fail + 1))
			printf 'FAIL [%s]\n  input: %s\n  want:  %s\n  got:   %s\n' "$bin" "$input" "$want" "$got"
		fi
	done
}

summary() {
	printf '%-10s passed %4d  failed %d\n' "$1" "$pass" "$fail"
	[ "$fail" -eq 0 ]
}

# build_ram <dir> <sanitizer flags>: RAM (bootstrapping) build of the Linux host
build_ram() {
	mkdir -p "$1" &&
	$CC $CFLAGS_COMMON $2 -o "$1/zforth" src/linux/main.c $HOST_SRCS -lm
}

# build_image <dir> <sanitizer flags> <extra cflags> [file.zf...]: ROM build
# with ext.zf and the given files baked in. Needs $BUILD/asan/zforth as the
# exporter. main.c is copied next to the header because it includes
# "zforth_dict.h" with quotes, which always looks in main.c's own directory
# first.
build_image() {
	local dir=$1 san=$2 extra=$3
	shift 3
	mkdir -p "$dir" &&
	"$BUILD/asan/zforth" -H zforth_dict forth/ext.zf "$@" > "$dir/zforth_dict.h" &&
	cp src/linux/main.c "$dir/main.c" &&
	$CC $CFLAGS_COMMON -I"$dir" $san -DZF_ENABLE_ROM_DICT=1 -DZF_LINUX_ROM_DICT=1 $extra \
		-o "$dir/zforth-rom" "$dir/main.c" $HOST_SRCS -lm
}
