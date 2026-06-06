
BIN	:= zforth
ROM_BIN	:= zforth-rom
SRC	:= main.c zforth.c

OBJS    := $(subst .c,.o, $(SRC))
ROM_OBJS := main_rom.o zforth_rom.o
DEPS    := $(subst .c,.d, $(SRC)) $(subst .o,.d, $(ROM_OBJS))

CC	:= $(CROSS)gcc

VPATH   := ../zforth
CFLAGS	+= -I. -I../zforth
CFLAGS  += -Os -g -pedantic -MMD
CFLAGS  += -fsanitize=address -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-result

ifneq ($(findstring clang,$(shell $(CC) --version 2>/dev/null)),clang)
CFLAGS  += -Wno-clobbered
endif
LDFLAGS	+= -fsanitize=address -g 

LIBS	+= -lm

ifndef noreadline
LIBS	+= -lreadline
CFLAGS	+= -DUSE_READLINE
endif

$(BIN): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

$(ROM_BIN): $(ROM_OBJS)
	$(CC) $(LDFLAGS) -o $@ $(ROM_OBJS) $(LIBS)

main_rom.o: main.c zforth_dict.h
	$(CC) $(CFLAGS) -DZF_ENABLE_ROM_DICT=1 -DZF_LINUX_ROM_DICT=1 -c -o $@ $<

zforth_rom.o: ../zforth/zforth.c
	$(CC) $(CFLAGS) -DZF_ENABLE_ROM_DICT=1 -DZF_LINUX_ROM_DICT=1 -c -o $@ $<

zforth_dict.h: $(BIN) ../../forth/bs.zf ../../forth/core.zf ../../forth/memaccess.zf
	cd ../.. && ./src/linux/zforth -H zforth_dict forth/bs.zf > src/linux/zforth_dict.h

clean:
	rm -f $(BIN) $(ROM_BIN) zforth_dict.h $(OBJS) $(ROM_OBJS) $(DEPS)

lint:
	lint -i /opt/flint/supp/lnt -i ..\\zforth -i src -w2 co-gcc.lnt \
		-e537 -e451 -e524 -e534 -e641 -e661 -e64 \
		$(SRC)

-include $(DEPS)
