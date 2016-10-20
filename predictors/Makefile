RAMSES_PATH := ../ramses

CC := gcc
CFLAGS := -std=gnu99 -Wall -Wpedantic -O2 -I$(RAMSES_PATH)/include

obj_files := $(patsubst %.c,%.o,$(wildcard *.c))

all: $(obj_files)

%.o: %.c

%.o: %.c %.h predictor.h
	$(CC) $(CFLAGS) -c $<

.PHONY: clean

clean:
	rm -f $(obj_files)
