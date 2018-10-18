/*
 * Copyright (c) 2017-2018 Vrije Universiteit Amsterdam
 *
 * This program is licensed under the GPL2+.
 */

#ifndef PRESSURE_H
#define PRESSURE_H 1

#include <stddef.h>

struct PresArg {
	void *buf;
	size_t buflen;
	size_t step;
};

enum pressure_fn {
	PRES_NONE = 0,
	PRES_SEQ_LD, /* Sequential loads */
	PRES_SEQ_ST /* Sequential stores */
};

struct PressureKernel {
	enum pressure_fn func;
	struct PresArg arg;
};

/* Return the storage size needed for the thread IDs of nthreads threads */
size_t pres_tids_size(size_t nthreads);
/*
 * Start a sequence of pressure threads and store their thread IDs in *tids.
 * pks is a PRES_NONE-terminated array of pressure kernels to be started.
 */
int pres_start(struct PressureKernel *pks, void *tids);
/* Stop pressure threads previously started by pres_start */
int pres_stop(struct PressureKernel *pks, void *tids);

#endif /* pressure.h */
