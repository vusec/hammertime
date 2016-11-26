libname := ramses
arname := lib$(libname).a
soname := lib$(libname).so
abi := 0

CC := gcc
CFLAGS := -std=gnu99 -Wall -Wpedantic -O2 -fPIC -Iinclude
LDFLAGS := -shared -Wl,-soname,$(soname).$(abi)

obj_files := $(patsubst %.c,%.o,$(wildcard *.c))
extra_headers := bitops.h include/$(libname)/types.h include/$(libname)/util.h include/$(libname)/vtlbucket.h

all: $(arname) $(soname)

# Override built-in compile rule
%.o: %.c

# Internal objects
%.o: %.c %.h $(extra_headers)
	$(CC) $(CFLAGS) -c $<

# Exported objects
%.o: %.c include/$(libname)/%.h $(extra_headers)
	$(CC) $(CFLAGS) -c $<

# Static lib
$(arname): $(obj_files)
	ar -rcs $@ $?

# Dynamic lib
$(soname): $(obj_files)
	$(CC) $(LDFLAGS) -o $@.$(abi) $^
	ln -sf $@.$(abi) $@

.PHONY: clean

clean:
	rm -f $(arname) $(soname) $(soname).$(abi) $(obj_files)
	rm -rf tools/__pycache__
