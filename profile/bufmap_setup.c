/*
 * Copyright (c) 2018 Vrije Universiteit Amsterdam
 *
 * This program is licensed under the GPL2+.
 */

#include "bufmap_setup.h"

#include <ramses/translate/heuristic.h>

static int setup_bufmap_heur(struct BufferMap *bm, void *buf, size_t len,
                             struct MemorySystem *msys, struct HeurArg *arg)
{
	struct Translation trans;
	ramses_translate_heuristic(&trans, arg->bits, arg->base);
	return ramses_bufmap(bm, buf, len, &trans, msys, 0);
}

#ifdef __linux__
#include <ramses/translate/pagemap.h>

#include <unistd.h>
#include <fcntl.h>

static int setup_bufmap_pagemap(struct BufferMap *bm, void *buf, size_t len,
                                struct MemorySystem *msys)
{
	int ret;
	int pagemap_fd;
	struct Translation trans;
	if ((pagemap_fd = open("/proc/self/pagemap", O_RDONLY)) == -1) {
		return 1;
	}
	ramses_translate_pagemap(&trans, pagemap_fd);
	ret = ramses_bufmap(bm, buf, len, &trans, msys, 0);
	close(pagemap_fd);
	return ret;
}
#endif /* Linux pagemap */

int setup_bufmap(struct BufferMap *bm, void *buf, size_t len,
                 struct MemorySystem *msys, enum TransType t, void *targ)
{
	switch (t) {
		case TRANS_HEUR:
			return setup_bufmap_heur(bm, buf, len, msys, (struct HeurArg *)targ);
		case TRANS_NATIVE:
		#ifdef __linux__
			return setup_bufmap_pagemap(bm, buf, len, msys);
		#endif
		default: return -1;
	}
	return -1;
}
