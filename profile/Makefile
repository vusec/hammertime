RAMSES_PATH := ../ramses

DEF_CFLAGS := -std=c99 -Wall -I$(RAMSES_PATH)/include -Wpedantic -pedantic
OFLAGS := -O2

CFLAGS := $(DEF_CFLAGS) $(EXTRA_CFLAGS) $(OFLAGS)
DOTOBUILD = $(CC) $(CFLAGS) -c $<

objs := main.o alloc.o hamqueue.o mem.o calibrate.o profile.o hammer.o pressure.o bufmap_setup.o params.o

all: profile

%.o: %.c
	$(DOTOBUILD)

%.o: %.c %.h
	$(DOTOBUILD)

calibrate.o: calibrate.c calibrate.h calibrate_time.h hammer.h
profile.o: profile.c profile.h hamqueue.h mem.h hammer.h
params.o: params.c params.h alloc.h hammer.h
main.o: main.c alloc.h bufmap_setup.h calibrate.h hammer.h profile.h pressure.h params.h

pressure.o: pressure.c pressure_pthread.h
	$(DOTOBUILD) -pthread


profile: $(objs)
	$(CC) $(OFLAGS) -o $@ $^ $(RAMSES_PATH)/libramses.a -pthread

.PHONY: clean cap

clean:
	rm -f $(objs) profile

cap: profile
	setcap cap_sys_admin,cap_dac_read_search,cap_ipc_lock+ep profile
