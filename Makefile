

all:
	$(MAKE) -C src/linux

linux-rom: src/linux/zforth-rom

zforth_dict.h: src/linux/zforth forth/ext.zf forth/core.zf forth/memaccess.zf forth/float.zf forth/double.zf forth/dfloat.zf
	./src/linux/zforth -H zforth_dict forth/ext.zf > $@

src/linux/zforth: FORCE
	$(MAKE) -C src/linux

src/linux/zforth-rom: FORCE
	$(MAKE) -C src/linux zforth-rom

test:
	bash tests/run.sh

clean:
	$(MAKE) -C src/linux clean
	$(MAKE) -C src/atmega8 clean
	rm -f zforth_dict.h
	rm -rf tests/build

.PHONY: all linux-rom test clean FORCE
FORCE:
