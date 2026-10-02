#!/usr/bin/env bash
# Dictionary size limits, using ROM builds with allocation tracing
# (tests/support/realloc_trace.h logs each allocation to stderr).
. "$(dirname "$0")/lib.sh"
T_SQUEEZE=1
TRACE="-include $ROOT/tests/support/realloc_trace.h"

lim() {   # lim <name> <cflags>: build a traced ROM image with vars.zf
	build_image "$BUILD/lim/$1" "$SAN_ASAN" "$TRACE $2" tests/images/vars.zf || { fail=$((fail + 1)); echo "FAIL: building $1"; }
	BINS="$BUILD/lim/$1/zforth-rom"
}

# grows in 2 KB steps up to the 8 KB cap, then aborts; the interpreter recovers
lim max8k "-DZF_DICT_MAX_SIZE=8192 -DZF_DICT_GROW_SIZE=2048"
t $': spin begin 1 , again ;\nspin\n1 2 + .\nempty\n: hi 5 . ; hi' '[realloc 4096] [realloc 6144] [realloc 8192] stdin:2: outside memory 3 5'
# the ROM image does not count toward the limit
t '7000 allot 1 , 1 2 + .' '[realloc 4096] [realloc 8192] 3'

# initial size = maximum: one allocation, never reallocated
lim once8k "-DZF_DICT_MAX_SIZE=8192 -DZF_DICT_INITIAL_SIZE=8192"
t $': spin begin 1 , again ;\nspin\n1 2 + .' '[realloc 8192] stdin:2: outside memory 3'

# a limit smaller than the data window: mounting fails cleanly
lim tiny "-DZF_DICT_MAX_SIZE=16"
t '1 2 + .' '[realloc 16] error mounting built-in ROM dictionary'

summary limits
