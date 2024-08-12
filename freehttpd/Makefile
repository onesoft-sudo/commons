SOURCES = $(wildcard *.c)
HEADERS = $(wildcard *.h) $(wildcard html/*.h)
OBJECTS = $(SOURCES:.c=.o)
BIN = freehttpd
CFLAGS = -g -O2 -Wall -Wextra -std=c23 -pedantic -pedantic-errors -pthread -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE
LDFLAGS = -pthread
LDLIBS = -lmagic
CC = gcc
SUBDIRS = html

all: $(SUBDIRS) $(BIN)

.PHONY: all clean $(SUBDIRS)

$(SUBDIRS):
	$(MAKE) -C $@

$(BIN): $(OBJECTS) $(HEADERS)

$(HEADERS): $(SOURCES)

clean:
	rm -f $(BIN) $(OBJECTS)
	for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir clean; \
	done