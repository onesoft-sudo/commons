CC = gcc
CFLAGS = -std=c99 -g -O2 -I. -Wall -Wextra -Werror -pedantic -DHAVE_CONFIG_H -DNDEBUG 
LDLIBS = -lm

all: uar
uar: uar.o main.o xmalloc.o

clean:
	rm -f uar *.o