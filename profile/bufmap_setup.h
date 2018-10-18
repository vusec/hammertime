/*
 * Copyright (c) 2018 Vrije Universiteit Amsterdam
 *
 * This program is licensed under the GPL2+.
 */

#ifndef BUFMAP_SETUP_H
#define BUFMAP_SETUP_H 1

#include <ramses/types.h>
#include <ramses/msys.h>
#include <ramses/bufmap.h>

enum TransType {
	TRANS_HEUR,
	TRANS_NATIVE
};

struct HeurArg {
	physaddr_t base;
	int bits;
};

int setup_bufmap(struct BufferMap *bm, void *buf, size_t len,
                 struct MemorySystem *msys, enum TransType t, void *targ);

#endif /* bufmap_setup.h */
