RAMSES_PATH := ../../ramses

CC := gcc
CFLAGS := -std=gnu99 -Wall -I$(RAMSES_PATH)/include
OFLAGS := -O3 -finline-functions

DOTOBUILD = $(CC) $(CFLAGS) $(OFLAGS) -c $<

all: profile

hammer.o: hammer.c
	$(DOTOBUILD)

addr.o: addr.c addr.h
	$(DOTOBUILD)

main.o: main.c hammer.h addr.h
	$(DOTOBUILD) -pthread


profile: main.o addr.o hammer.o
	$(CC) $(OFLAGS) -o $@ $^ $(RAMSES_PATH)/libramses.a -pthread


.PHONY: clean cap

clean:
	rm -f *.o profile

cap: profile
	setcap cap_sys_admin,cap_dac_read_search,cap_ipc_lock+ep profile
