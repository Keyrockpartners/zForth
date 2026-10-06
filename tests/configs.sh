#!/usr/bin/env bash
# Build-config matrix: zforth.c must compile cleanly in each supported
# configuration, and the compile-time checks must reject unsupported ones.
. "$(dirname "$0")/lib.sh"
W="-Wall -Wextra -Werror -pedantic -Wno-unused-parameter -Wno-unused-result -I$ROOT/src/zforth"
OUT=$BUILD/configs
mkdir -p "$OUT"
L=$ROOT/src/linux
D=$ROOT/tests/support/devconf

try() {   # try <ok|fail> <config dir> <flags>
	local got
	if eval "$CC" "$W" -I"$2" "$3" -c -o "$OUT/cfg.o" src/zforth/zforth.c 2> "$OUT/cfg.err"; then got=ok; else got=fail; fi
	if [ "$got" = "$1" ]; then
		pass=$((pass + 1))
	else
		fail=$((fail + 1))
		printf 'FAIL: expected %s, got %s: %s %s\n' "$1" "$got" "${2#$ROOT/}" "$3"
		head -5 "$OUT/cfg.err"
	fi
}

try ok "$L" ""
try ok "$L" "-DZF_ENABLE_ROM_DICT=1"
try ok "$L" "-DZF_ENABLE_DYNAMIC_DICT=0"
try ok "$L" "-DZF_ENABLE_DYNAMIC_DICT=0 -DZF_ENABLE_ROM_DICT=1"
try ok "$L" "-DZF_ENABLE_FLOAT=0"
try ok "$L" "-DZF_ENABLE_DOUBLE_CELL=0"
try ok "$L" "-DZF_ENABLE_DFLOAT=0"
try ok "$L" "-DZF_ENABLE_NAMED_LOCALS=0"
try ok "$L" "-DZF_EXT_PRIMS_HEADER='\"ext_test.h\"' -I$ROOT/tests/support/extprims"
try ok "$L" "-DZF_ENABLE_FLOAT=0 -DZF_ENABLE_DOUBLE_CELL=0"
try ok "$D" "-DZF_ENABLE_ROM_DICT=1"
try ok "$D" "-DZF_ENABLE_ROM_DICT=1 -DZF_ENABLE_FLOAT=0"
try ok "$D" "-DZF_ENABLE_ROM_DICT=1 -DZF_ENABLE_FLOAT=0 -DZF_ENABLE_DOUBLE_CELL=0 -DZF_ENABLE_DFLOAT=1"
try ok "$D" "-DZF_ENABLE_ROM_DICT=1 -DZF_ENABLE_DOUBLE_CELL=0 -DZF_ENABLE_FLOAT=0 -DZF_ENABLE_DFLOAT=0"
# unsupported cell types are rejected at compile time
try fail "$L" "-DZF_CELL_TYPE=double"
try fail "$L" "-DZF_CELL_TYPE=int64_t -DZF_UCELL_TYPE=uint64_t"
try fail "$L" "-DZF_CELL_TYPE=int64_t -DZF_UCELL_TYPE=uint64_t -DZF_ENABLE_FLOAT=0 -DZF_CELL_FMT='\"%\" PRId64'"
try ok "$L" "-DZF_CELL_TYPE=int64_t -DZF_UCELL_TYPE=uint64_t -DZF_ENABLE_FLOAT=0 -DZF_ENABLE_DOUBLE_CELL=0 -DZF_ENABLE_DFLOAT=0 -DZF_CELL_FMT='\"%\" PRId64'"

summary configs
