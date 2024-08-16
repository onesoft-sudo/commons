ARCH_LIST = x86_64
LIB = lib/libc.a
BIN = bin-exec

export AS = as
export CC = gcc
export CFLAGS = -g -Wall -Wextra -Werror -std=c99 -pedantic -O2 -static -nostdlib -nostartfiles -ffreestanding
export LDFLAGS = -g -static -nostdlib -nostartfiles -e _start -ffreestanding
export ASFLAGS = -g
export RM = rm -f

all: $(ARCH_LIST) lib
	
lib:
	$(MAKE) -C lib

.PHONY: lib $(ARCH_LIST)

$(ARCH_LIST):
	$(MAKE) -C $@

clean:
	$(RM) *.o $(patsubst %,%/*.o,$(ARCH_LIST))
	$(MAKE) -C lib clean

	for dir in $(ARCH_LIST); do \
		if [ -f $$dir/Makefile ]; then \
			$(MAKE) -C $$dir clean; \
		fi; \
	done