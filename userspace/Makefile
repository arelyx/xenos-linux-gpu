CC      ?= gcc
CFLAGS  ?= -O2 -g -fwrapv -Wall -Wno-unused -Wno-pointer-sign -Wno-int-conversion -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast -fno-strict-aliasing
CPPFLAGS = -Ishim -Ilibxe -I../include
LIBXE    = libxe/xe.o libxe/ucode.o libxe/xe_linux.o libxe/edram.o
TESTS    = tests/xeprobe tests/xeclear tests/xecube tests/xediag tests/xebench tests/xeinit tests/xerecord tests/msmtest tests/xereg tests/xereplay

all: $(TESTS)

libxe/%.o: libxe/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

tests/%: tests/%.c $(LIBXE)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< $(LIBXE) -lm

tests/xecube: tests/xecube.c tests/engine.c tests/xee.c $(LIBXE)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/xecube.c tests/engine.c tests/xee.c $(LIBXE) -lm

clean:
	rm -f libxe/*.o $(TESTS)
