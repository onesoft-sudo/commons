CC = gcc
CFLAGS = -Wall -Wextra -Werror -std=c23 -pedantic -g -O2 -DHAVE_CONFIG_H -I. -I.. -D_POSIX_C_SOURCE=200112L -D_GNU_SOURCE
BINS = srcstats

all: srcproc
srcproc: $(BINS)

srcstats: srcstats.o

clean:
	rm -f *.o $(BINS)