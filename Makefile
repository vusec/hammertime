RAMSES_PATH := ./ramses

CC := gcc
CFLAGS := -std=gnu99 -Wall -Wpedantic -O2 -I$(RAMSES_PATH)/include
DEMOCFLAGS := $(CFLAGS) -pthread
DEMOLFLAGS := -L./perfev-util -lperfev-util -lpfm

demo_objs := predictors/*.o probes/perfev/*.o $(RAMSES_PATH)/libramses.a

obj_files := $(filter-out demo.o, $(patsubst %.c,%.o,$(wildcard *.c)))

build_subdirs := perfev-util predictors probes
extra_subdirs := ramses tools fliptables

build_files := $(obj_files) $(build_subdirs) ramses tools

default: $(build_files)

all: default demo

%.o: %.c

%.o: %.c %.h
	$(CC) $(CFLAGS) -c $<

demo: demo.c $(obj_files) $(build_subdirs) fliptables
	$(CC) $(DEMOCFLAGS) -o $@ $< $(obj_files) $(demo_objs) $(DEMOLFLAGS)

.PHONY: clean cleanall $(build_subdirs) $(extra_subdirs)

probes: perfev-util

$(build_subdirs): ramses
	@$(MAKE) -C $@

ramses:
	@$(MAKE) -C $@

tools: $(build_subdirs) ramses
	@$(MAKE) -C $@

fliptables:
	@$(MAKE) -C $@

clean:
	@for d in $(build_subdirs) ramses tools; do $(MAKE) -C $$d clean; done
	rm -f $(obj_files) demo

cleanall: clean
	@$(MAKE) -C fliptables clean
