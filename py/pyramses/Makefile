RAMSES_DIR = ../../ramses
RAMSES_LIB = libramses.so

all: $(RAMSES_LIB)

$(RAMSES_LIB): $(RAMSES_DIR)/$(RAMSES_LIB)
	cp $< $@

.PHONY: clean

clean:
	rm -f $(RAMSES_LIB)
	rm -rf __pycache__
