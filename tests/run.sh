#!/usr/bin/env bash
# Run all zForth tests: `make test`, or `bash tests/run.sh` from anywhere.
# Builds its own binaries under tests/build (the src/linux build is untouched),
# then runs the unit suites on the RAM and ROM builds and again on a UBSan
# build, the ROM image tests, the dictionary-limit tests and the build-config
# matrix. Exits non-zero if anything fails.
. "$(dirname "$0")/lib.sh"

echo "building test binaries with $CC"
build_ram "$BUILD/asan" "$SAN_ASAN" &&
build_image "$BUILD/asan/stock" "$SAN_ASAN" "" &&
build_ram "$BUILD/ubsan" "$SAN_UBSAN" || { echo "build failed"; exit 1; }

status=0
for s in tests/suites/*.sh; do
	bash "$s" || status=1
done
echo "-- again under UBSan (RAM build)"
for s in tests/suites/*.sh; do
	ZF_TEST_BINS="$BUILD/ubsan/zforth forth/ext.zf" bash "$s" || status=1
done
echo "--"
bash tests/images.sh || status=1
bash tests/host.sh || status=1
bash tests/limits.sh || status=1
bash tests/configs.sh || status=1

if [ $status -eq 0 ]; then echo "all tests passed"; else echo "SOME TESTS FAILED"; fi
exit $status
