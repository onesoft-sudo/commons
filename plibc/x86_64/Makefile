ARCH = x86_64
SOURCES = $(wildcard *.s)
OBJECTS = $(SOURCES:.s=.o)
LIB = lib$(ARCH).a

all: $(ARCH)

$(ARCH): $(OBJECTS)
	@echo "  AR    $(LIB)"
	@$(AR) rcs $(LIB) $(OBJECTS)

%.o: %.s
	@echo "  AS     $@"
	@$(AS) $(ASFLAGS) -o $@ $<

clean:
	@echo "  RM     $(LIB)"
	@$(RM) $(LIB)

	@for file in *.o; do \
		echo "  RM     $$file"; \
		$(RM) $$file; \
	done