LIB = libc.a
OBJECTS = stdio.o malloc.o errno.o unistd.o string.o boot.o stdlib.o
CFLAGS = -g -I. -Wall -Wextra -Werror -std=c99 -pedantic -O2 -static -nostdlib -nostartfiles -ffreestanding
CC = gcc
AR = ar
RANLIB = ranlib
RM = rm -f
LDFLAGS = -g  -static -nostdlib -nostartfiles -ffreestanding

all: $(LIB)

$(LIB): $(OBJECTS)
	@echo "  AR     $@"
	@$(AR) rcs $@ $^
	@echo "  RANLIB $@"
	@$(RANLIB) $@

%.o: %.c
	@echo "  CC     $@"
	@$(CC) $(CFLAGS) -c -o $@ $<

clean:
	@echo "  RM     $(LIB)"
	@$(RM) $(LIB)

	@for file in *.o; do \
		echo "  RM     $$file"; \
		$(RM) $$file; \
	done