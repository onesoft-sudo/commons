LIB = libuar.a
OBJECTS = libuar.o stdio.o malloc.o
CFLAGS = -g -Wall -Wextra -std=c99 -pedantic -O2 -static -nostdlib -nostartfiles -ffreestanding
CC = gcc
AR = ar
RANLIB = ranlib
RM = rm -f
LDFLAGS = -g

all: $(LIB)

$(LIB): $(OBJECTS)
	$(AR) rcs $@ $^
	$(RANLIB) $@

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	$(RM) $(OBJECTS) $(LIB)