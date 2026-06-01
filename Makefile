

all:
	$(MAKE) -C src/linux

zforth_dict.h: src/linux/zforth forth/bs.zf forth/core.zf forth/memaccess.zf
	./src/linux/zforth -H zforth_dict forth/bs.zf > $@

src/linux/zforth: FORCE
	$(MAKE) -C src/linux

clean:
	$(MAKE) -C src/linux clean
	$(MAKE) -C src/atmega8 clean
	rm -f zforth_dict.h

.PHONY: all clean FORCE
FORCE:
