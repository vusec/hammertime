/*
 * Copyright (c) 2017-2018 Vrije Universiteit Amsterdam
 *
 * This program is licensed under the GPL2+.
 */

#ifndef PRESSURE_PTHREAD_H
#define PRESSURE_PTHREAD_H 1

#include "pressure.h"

#include <pthread.h>

typedef void *(*pressure_fn_t)(void *arg);

static const size_t CHECK_PERIOD = 1 << 24;

static void *pres_seq_ld(void *arg)
{
	struct PresArg a = *((struct PresArg *)arg);
	volatile char *buf = a.buf;
	size_t idx = 0;
	for (size_t i = 0; ; i++) {
		idx = (idx + a.step) % a.buflen;
		(void)buf[idx];
		if (!(i % CHECK_PERIOD)) {
			pthread_testcancel();
		}
	}
	return NULL;
}

static void *pres_seq_st(void *arg)
{
	struct PresArg a = *((struct PresArg *)arg);
	volatile char *buf = a.buf;
	size_t idx = 0;
	for (size_t i = 0; ; i++) {
		idx = (idx + a.step) % a.buflen;
		buf[idx] = 0x55;
		if (!(i % CHECK_PERIOD)) {
			pthread_testcancel();
		}
	}
	return NULL;
}

static pressure_fn_t pfuncs[] = {
	[PRES_NONE] = NULL,
	[PRES_SEQ_LD] = pres_seq_ld,
	[PRES_SEQ_ST] = pres_seq_st
};

size_t pres_tids_size(size_t nthreads)
{
	return nthreads * sizeof(pthread_t);
}

int pres_start(struct PressureKernel *pks, void *tids)
{
	pthread_t *threads = (pthread_t *)tids;
	int r;
	for (size_t i = 0; pks[i].func != PRES_NONE; i++) {
		if ((r = pthread_create(&threads[i], NULL,
		                        pfuncs[pks[i].func], &pks[i].arg)))
		{
			for (size_t j = 0; j < i; j++) {
				pthread_cancel(threads[j]);
			}
			return r;
		}
	}
	return 0;
}

int pres_stop(struct PressureKernel *pks, void *tids)
{
	pthread_t *threads = (pthread_t *)tids;
	int r;
	size_t tcnt;
	for (tcnt = 0; pks[tcnt].func != PRES_NONE; tcnt++) {
		pthread_cancel(threads[tcnt]);
	}
	for (size_t i = 0; i < tcnt; i++) {
		if ((r = pthread_join(threads[i], NULL))) {
			return r;
		}
	}
	return 0;
}

#endif /* pressure_pthread.h */
