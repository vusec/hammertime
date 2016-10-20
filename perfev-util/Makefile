libname := perfev-util
arname := lib$(libname).a

CC := gcc
CFLAGS := -std=gnu99 -Wall -Wpedantic -O2 -fPIC

obj_files := $(patsubst %.c,%.o,$(wildcard *.c))

all: $(arname)

# Override built-in compile rule
%.o: %.c

%.o: %.c %.h
	$(CC) $(CFLAGS) -c $<

# Static lib
$(arname): $(obj_files)
	ar -rcs $@ $?

.PHONY: clean

clean:
	rm -f $(arname) $(obj_files)

