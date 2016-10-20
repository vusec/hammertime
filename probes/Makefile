hammtime_path := ..
peutil_path := $(hammtime_path)/perfev-util
RAMSES_PATH := $(hammtime_path)/ramses

CC := gcc
CFLAGS := -std=gnu99 -Wall -Wpedantic -O2 -I$(RAMSES_PATH)/include -pthread

obj_files := $(patsubst %.c,%.o,$(wildcard *.c))
subdirs := perfev

all: $(obj_files) $(subdirs)

%.o: %.c

%.o: %.c %.h probe.h
	$(CC) $(CFLAGS) -c $<


.PHONY: clean cap $(subdirs)

$(subdirs):
	@$(MAKE) -C $@

clean:
	@for d in $(subdirs); do $(MAKE) -C $$d clean; done
	rm -f $(obj_files)
