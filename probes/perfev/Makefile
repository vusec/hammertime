hammtime_path := ../..
RAMSES_PATH := $(hammtime_path)/ramses

INCFLAGS := -I$(RAMSES_PATH)/include -I$(hammtime_path)

CC := gcc
CFLAGS := -std=gnu99 -Wall -Wpedantic -O2 $(INCFLAGS) -pthread

obj_files := $(patsubst %.c,%.o,$(wildcard *.c))

all: $(obj_files)

%.o: %.c

%.o: %.c %.h
	$(CC) $(CFLAGS) -c $<

.PHONY: clean

clean:
	rm -f $(obj_files)
