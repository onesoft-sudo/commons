ARCH_LIST = x86_64 armv7l aarch64
AS = as
CC = gcc
CFLAGS = -g -Wall -Wextra -Werror -std=c99 -pedantic -O2
LDFLAGS = -g -static -nostdlib -nostartfiles -e _start -ffreestanding
LIB = lib/libuar.a
ASFLAGS = -g
BIN = uar-asm

all: lib $(ARCH_LIST)

lib:
	$(MAKE) -C lib

.PHONY: lib

x86_64: $(BIN)-x86_64

armv7l: $(BIN)-armv7l

aarch64: $(BIN)-aarch64

$(BIN)-x86_64: x86_64/main.o x86_64/stdio.o x86_64/syscalls.o $(LIB)
	$(CC) $(LDFLAGS) -o $@ $^

$(BIN)-armv7l: armv7l/$(BIN)-armv7l.o $(LIB)
	$(CC) $(LDFLAGS) -o $@ $^

$(BIN)-aarch64: aarch64/$(BIN)-aarch64.o $(LIB)
	$(CC) $(LDFLAGS) -o $@ $^

clean:
	rm -f *.o $(patsubst %,%/*.o,$(ARCH_LIST))
	$(MAKE) -C lib clean