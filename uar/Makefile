CC = gcc
CFLAGS = -std=c99 -g -O2 -I. -Wall -Wextra -Werror -pedantic -DHAVE_CONFIG_H -DUAR_PRINT_VERBOSE_IMPL_INFO

all: uar
uar: uar.o main.o xmalloc.o

clean:
	rm -f uar *.o