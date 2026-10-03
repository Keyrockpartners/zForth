#!/usr/bin/env bash
# Linux host features: -e and file ordering, -x, the exit status, the
# fmt-buf syscall, ZF_DICT_HEADER, the image configuration check, and a host
# program with its own syscalls (src/linux/host.h).
. "$(dirname "$0")/lib.sh"

ZF=$BUILD/asan/zforth
TMP=$BUILD/host
mkdir -p "$TMP"
printf ': hi ." hi" ;\n' > "$TMP/a.zf"
printf '1 0 /\n' > "$TMP/bad.zf"

c() {   # c '<expected output>' <command...>: run, compare stdout+stderr
	local want=$1 got
	shift
	got=$("$@" 2>&1 </dev/null | sed $'s/\x1b\\[[0-9;]*m//g' | tr '\n' ' ' | sed 's/^ *//; s/ *$//')
	if [ "$got" = "$want" ]; then pass=$((pass + 1)); else
		fail=$((fail + 1)); printf 'FAIL: %s\n  want: %s\n  got:  %s\n' "$*" "$want" "$got"; fi
}
st() {  # st <expected status> <command...>
	local want=$1 got
	shift
	"$@" >/dev/null 2>&1 </dev/null; got=$?
	if [ "$got" = "$want" ]; then pass=$((pass + 1)); else
		fail=$((fail + 1)); printf 'FAIL: status %s, want %s: %s\n' "$got" "$want" "$*"; fi
}

c 'hi' "$ZF" -q -x forth/ext.zf "$TMP/a.zf" -e hi
c '1 hi2' "$ZF" -q -x forth/ext.zf -e '1 .' "$TMP/a.zf" -e hi -e '2 .'
c '-e:1: not a word' "$ZF" -q -x forth/ext.zf -e hi
c 'hi' "$ZF" -x -q forth/ext.zf -e 'cr' "$TMP/a.zf" -e hi
st 0 "$ZF" -q -x forth/ext.zf -e '1 drop'
st 1 "$ZF" -q -x forth/ext.zf -e nosuchword
st 1 "$ZF" -q -x forth/ext.zf "$TMP/bad.zf"
st 1 "$ZF" -q -x forth/ext.zf "$TMP/missing.zf"
st 1 "$ZF" -q -x -e
# fmt-buf: arguments, truncation, the count
c 'x=42 é|7' "$ZF" -q -x forth/ext.zf -e '16 buffer: b b 16 42 s" x=%d é" fmt-buf b over tell 124 emit .'
c 'abc3' "$ZF" -q -x forth/ext.zf -e '16 buffer: b b 3 s" abcdef" fmt-buf b over tell .'
c '0' "$ZF" -q -x forth/ext.zf -e '16 buffer: b b 0 7 s" %d" fmt-buf .'
c '1.50|  ab|-5|ffffffff21' "$ZF" -q -x forth/ext.zf -e '64 buffer: b b 64 1.5 s" ab" -5 -1 s" %.2f|%4s|%d|%x" fmt-buf b over tell .'
c '1234567890111' "$ZF" -q -x forth/ext.zf -e '64 buffer: b b 64 12345678901. s" %ld" fmt-buf b over tell .'
c '-e:1: outside memory' "$ZF" -q -x forth/ext.zf -e '0x30000000 100 s" xyz" fmt-buf'
# a ROM image selected with ZF_DICT_HEADER
mkdir -p "$TMP/img"
"$ZF" -H zforth_dict forth/ext.zf "$TMP/a.zf" > "$TMP/img/custom.h"
if $CC $CFLAGS_COMMON $SAN_ASAN -DZF_ENABLE_ROM_DICT=1 -DZF_LINUX_ROM_DICT=1 -DZF_DICT_HEADER="\"$TMP/img/custom.h\"" \
	-o "$TMP/img/zforth-rom" src/linux/main.c $HOST_SRCS -lm; then
	c 'hi' "$TMP/img/zforth-rom" -q -x -e hi
else
	fail=$((fail + 1)); echo "FAIL: building with ZF_DICT_HEADER"
fi
st 1 "$ZF" -H x forth/ext.zf "$TMP/bad.zf"
# an image only works on a build with the same feature flags
if $CC $CFLAGS_COMMON $SAN_ASAN -DZF_ENABLE_ROM_DICT=1 -DZF_LINUX_ROM_DICT=1 -DZF_ENABLE_NAMED_LOCALS=0 -DZF_DICT_HEADER="\"$TMP/img/custom.h\"" \
	-o "$TMP/img/zforth-rom-nol" src/linux/main.c $HOST_SRCS -lm; then
	c 'error mounting built-in ROM dictionary: built with different feature flags' "$TMP/img/zforth-rom-nol" -q -x -e hi
	st 1 "$TMP/img/zforth-rom-nol" -q -x -e hi
else
	fail=$((fail + 1)); echo "FAIL: building a ROM host with ZF_ENABLE_NAMED_LOCALS=0"
fi
rm -f zforth.save
"$ZF" -q -x forth/ext.zf "$TMP/a.zf" -e save && mv zforth.save "$TMP/a.save"
c 'hi' "$ZF" -q -x -l "$TMP/a.save" -e hi
if $CC $CFLAGS_COMMON $SAN_ASAN -DZF_ENABLE_NAMED_LOCALS=0 -o "$TMP/zforth-nol" src/linux/main.c $HOST_SRCS -lm; then
	c "error loading dictionary '$TMP/a.save': built with different feature flags" "$TMP/zforth-nol" -q -x -l "$TMP/a.save"
else
	fail=$((fail + 1)); echo "FAIL: building with ZF_ENABLE_NAMED_LOCALS=0"
fi
# the IDs from 134 up are the host application's
c 'unhandled syscall 134' "$ZF" -q -x forth/ext.zf -e '134 sys'
# a host program built from the reusable pieces with its own syscalls
cat > "$TMP/own.c" <<'EOF2'
#include "host.h"
static int own_sys(zf_ctx *ctx, zf_syscall_id id)
{
	if(id != 200) return 0;
	zf_push(ctx, zf_pop(ctx) * 2);
	return 1;
}
int main(int argc, char **argv)
{
	zfl_config cfg = { NULL, 0, 0, own_sys, NULL, 0 };
	return zfl_main(argc, argv, &cfg);
}
EOF2
# a host with a poll function: called while the REPL waits for input; the
# REPL still ends with its input
cat > "$TMP/poller.c" <<'EOF2'
#include <stdlib.h>
#include "host.h"
static int calls;
static void poller(zf_ctx *ctx)
{
	if(++calls == 3) zfl_eval(ctx, NULL, 0, "99 .");
	if(calls == 30) exit(0);
}
int main(int argc, char **argv)
{
	zfl_config cfg = { NULL, 0, 0, NULL, poller, 5 };
	return zfl_main(argc, argv, &cfg);
}
EOF2
if $CC $CFLAGS_COMMON $SAN_ASAN -o "$TMP/own" "$TMP/own.c" $HOST_SRCS -lm; then
	c '42' "$TMP/own" -q -x forth/ext.zf -e '21 200 sys .'
	c 'unhandled syscall 201' "$TMP/own" -q -x forth/ext.zf -e '201 sys'
	c '7' "$TMP/own" -q -x forth/ext.zf -e '16 buffer: b b 16 7 s" %d" fmt-buf b swap tell'
else
	fail=$((fail + 1)); echo "FAIL: building a host with its own syscalls"
fi
if $CC $CFLAGS_COMMON $SAN_ASAN -o "$TMP/poller" "$TMP/poller.c" $HOST_SRCS -lm; then
	c '99' sh -c "sleep 1 | '$TMP/poller' -q forth/ext.zf"
	c '7  99' sh -c "(printf '7 .\n'; sleep 1) | '$TMP/poller' -q forth/ext.zf"
	c '8  end' sh -c "printf '8 .' | '$TMP/poller' -q forth/ext.zf; echo end"
	c '-x:' sh -c "'$TMP/poller' -q -x forth/ext.zf; echo -x:"
else
	fail=$((fail + 1)); echo "FAIL: building a host with a poll function"
fi

summary host
