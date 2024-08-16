CC = gcc
CFLAGS = -std=c99 -g -O2 -I. -I../include -Wall -Wextra -Werror -pedantic -DHAVE_CONFIG_H -DNDEBUG -DHAVE_PLIBC
LDLIBS = -lm
RM = rm -f
AS = as
ASFLAGS = -g

all: uar bin-exec

uar: libuar.a main.o
	@echo "  CCLD  $@"
	@$(CC) $(CFLAGS) -o $@ main.o -L. -luar $(LDLIBS)

bin-exec: libuar.a selfext.o selfext_entry.o
	@echo "  CCLD  $@"
	@$(CC) $(CFLAGS) -I../plibc/lib -ffreestanding -nostdlib -e _start -o $@ selfext.o selfext_entry.o ../plibc/lib/libc.a ../plibc/x86_64/libx86_64.a -L. -luar $(LDLIBS)

%.o: %.s
	@echo "  AS    $@"
	@$(AS) $(ASFLAGS) -c -o $@ $<

%.o: %.c
	@echo "  CC    $@"
	@$(CC) $(CFLAGS) -c -o $@ $<

libuar.a: uar.o xmalloc.o
	@echo "  AR    $@"
	@ar rcs $@ $^

clean:
	@echo "  RM bin-exec"
	@echo "  RM uar"
	@echo "  RM *.o"
	@$(RM) uar *.o bin-exec *.a