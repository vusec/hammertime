hammtime_path := ../..
peutil_path := $(hammtime_path)/perfev-util
RAMSES_PATH := $(hammtime_path)/ramses

CC := gcc
CFLAGS := -std=gnu99 -Wall -Wpedantic -O2 -I$(RAMSES_PATH)/include -I$(hammtime_path) -pthread

all: probe_test

%.o: %.c
	$(CC) $(CFLAGS) -c $<

%.o: %.c %.h
	$(CC) $(CFLAGS) -c $<

probe_test: probe_test.o probe_logger.o
	$(CC) $(CFLAGS) -o $@ -L$(RAMSES_PATH) -L$(peutil_path) \
	$^ $(hammtime_path)/probes/perfev/*.o -lramses -lperfev-util -lpfm

.PHONY: clean cap

clean:
	rm -f *.o probe_test

cap:
	setcap cap_sys_admin+ep probe_test
