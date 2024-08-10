SOURCES = $(wildcard *.c)
OBJECTS = $(SOURCES:.c=.o)
BIN = freehttpd
CFLAGS = -g -O2 -Wall -Wextra -Werror -std=c23 -pedantic -pedantic-errors -pthread
LDFLAGS = -pthread
CC = gcc

all: $(BIN)

$(BIN): $(OBJECTS)

clean:
	rm -f $(BIN) $(OBJECTS)