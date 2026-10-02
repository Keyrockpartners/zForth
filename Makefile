

all:
	$(MAKE) -C src/linux

linux-rom: src/linux/zforth-rom

zforth_dict.h: src/linux/zforth forth/bs.zf forth/core.zf forth/memaccess.zf forth/float.zf forth/double.zf forth/dfloat.zf
	./src/linux/zforth -H zforth_dict forth/bs.zf > $@

src/linux/zforth: FORCE
	$(MAKE) -C src/linux

src/linux/zforth-rom: FORCE
	$(MAKE) -C src/linux zforth-rom

# ZGo: the compiler, the example binaries and the ZGo tests
ZGOC := build/zgoc

zgoc: $(ZGOC)

$(ZGOC): FORCE
	cd zgo && go build -o ../$(ZGOC) ./cmd/zgoc

# Build every example as a standalone binary with its program in a ROM
# image: build/examples/<name>/<name>
examples: $(ZGOC) src/linux/zforth
	@for d in examples/*/; do \
		n=$$(basename $$d); \
		$(ZGOC) image -o build/examples/$$n $$d*.zgo || exit 1; \
		echo "built build/examples/$$n/$$n"; \
	done

# Go unit tests, the examples in run and image mode (and the images again
# under UBSan), and the error tests
test-zgo: $(ZGOC) src/linux/zforth
	cd zgo && go test ./...
	$(ZGOC) test examples/* zgo/testdata/errors
	$(ZGOC) test -san ubsan -build build/zgo-test-ubsan examples/*

test-zforth:
	bash tests/run.sh

test: test-zforth test-zgo

clean:
	$(MAKE) -C src/linux clean
	$(MAKE) -C src/atmega8 clean
	rm -f zforth_dict.h
	rm -rf tests/build build

.PHONY: all linux-rom zgoc examples test test-zforth test-zgo clean FORCE
FORCE:
