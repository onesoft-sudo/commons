HTML_FILES = $(wildcard *.html)
HEADERS = $(HTML_FILES:.html=.h)

all: $(HEADERS)

$(HEADERS): $(HTML_FILES)
	xxd -i $< > $@
	sed -i 's/^unsigned/static unsigned/' $@

clean:
	rm -f *.h