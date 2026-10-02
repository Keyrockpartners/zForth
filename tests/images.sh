#!/usr/bin/env bash
# ROM image tests. Each fixture in tests/images/ is run two ways against the
# same expectations: loaded as source by the RAM build, and baked into a ROM
# image (as device firmware would be). Some checks only apply to ROM.
. "$(dirname "$0")/lib.sh"
T_SQUEEZE=1

image() {   # image <fixture>: build its ROM image, test both modes
	build_image "$BUILD/img/$1" "$SAN_ASAN" "" "tests/images/$1.zf" || { fail=$((fail + 1)); echo "FAIL: building image $1"; }
	RAM="$BUILD/asan/zforth forth/bs.zf tests/images/$1.zf"
	ROM="$BUILD/img/$1/zforth-rom"
	BINS="$RAM|$ROM"
}
rom_only() { BINS=$ROM; }

# variables and a buffer defined at export time stay writable in ROM,
# with their export-time values
image vars
t 'foo @ . bar @ . small @ .' '42 123 1'
t '1000000 small ! 3.25 foo ! small @ . foo @ f. bar @ .' '1000000 3.25 123'
t 'buf 5 tell' 'hello'
t '20000 allot var z 5 z ! z @ . 1 2 + .' '5 3'
rom_only
t 'buf 0x40000000 u>= . buf 4 + @u8 .' '-1 111'
t 'buf 5 + @u8 .' 'stdin:1: outside memory'
t '1 buf 5 + !u8' 'stdin:1: outside memory'
t 'buf 6 tell' 'stdin:1: outside memory'
t 'chkpt base 7 variable rt rt @ . 8 rt ! rt @ . base' '7 8'
t 'chkpt base 7 variable rt base rt' 'stdin:1: not a word'
t 'save' 'dictionary dump unavailable for prebuilt dictionary'

# buffer:, and a checkpoint baked into the image
image buffers
t 'msg 3 tell 8 buffer: b2 1 b2 7 + !u8 b2 7 + @u8 .' 'hi!1'
t '9 foo ! : hi 1 . ; hi base hi' 'stdin:1: not a word 1'
t '9 foo ! 3 variable v base foo @ . msg 3 tell : again 2 . ; again base 1 2 + .' '9 hi!2 3'
t 'base base 7 variable w w @ .' '7'

image doubles
t 'big ud. bigv 2@ ud. 1. bigv 2! bigv 2@ ud.' '123456789012 98765432109 1'

image dfloats
t 'avo df. lat df@ df. -122.6765432d0 lat df! lat df@ df.' '6.02e+23 45.1234567 -122.6765432'

image locals
t '3 4 hyp . 1700000000000. 5. dsum d. 10 fact . : rt {: d: v :} total 2@ v d+ total 2! ; 5. rt 7. rt total 2@ d.' '25 1700000000005 3628800 12'

summary images
