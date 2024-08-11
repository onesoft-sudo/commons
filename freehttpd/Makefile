SOURCES = $(wildcard *.c)
HEADERS = $(wildcard *.h)
OBJECTS = $(SOURCES:.c=.o)
BIN = freehttpd
CFLAGS = -g -O2 -Wall -Wextra -Werror -std=c23 -pedantic -pedantic-errors -pthread -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE
LDFLAGS = -pthread
LDLIBS = -lmagic
CC = gcc

all: $(BIN)

$(BIN): $(OBJECTS) $(HEADERS)

$(HEADERS): $(SOURCES)

clean:
	rm -f $(BIN) $(OBJECTS)