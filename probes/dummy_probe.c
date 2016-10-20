/*
 * Copyright (c) 2016 Andrei Tatar
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "dummy_probe.h"

#include "probe.h"

#include <stdlib.h>

#include <unistd.h>
#include <pthread.h>

/* Testing */
//~ #define BURSTLEN 5
//~ #define USLEEPTIME 10000

/* Rowhammer-like throughput */
#define BURSTLEN 40000
#define USLEEPTIME 5000

struct DummyState {
	struct ProbeOutput *out;
	pthread_t probe_thread;
	int started;
};

void *genaddr(void *arg)
{
	int wup = 0;
	uint64_t h = 0;
	struct ProbeOutput *o = (struct ProbeOutput *)arg;
	uint8_t *dp = o->data;
	size_t sz = o->data_size;
	for (;;) {
		usleep(USLEEPTIME);
		wup++;
		h = o->head;
		for (int i = 0; i < BURSTLEN; i++) {
			int x = random();
			*(uint64_t *)(dp + (h % sz)) = (uint64_t)x;
			h += sizeof(uint64_t);
			if (o->fmtflags & PROBEOUT_VIRTADDR) {
				*(uint64_t *)(dp + (h % sz)) = (uint64_t)~x;
				h += sizeof(uint64_t);
			}
		}

		if (!(wup & 0xf)) {
			*(uint64_t *)(dp + (h % sz)) = (uint64_t)-1;
			h += sizeof(uint64_t);
			*(uint64_t *)(dp + (h % sz)) = (uint64_t)wup;
			h += sizeof(uint64_t);
		}

		pthread_mutex_lock(&o->update_mutex);
		o->head = h;
		pthread_cond_broadcast(&o->update_cond);
		pthread_mutex_unlock(&o->update_mutex);
	}
}

static int not_implemented(void *ctx)
{
	return PROBE_CPFUNC_NOTIMPL;
}

static int dummy_status(void *ctx)
{
	struct DummyState *st = (struct DummyState *)ctx;
	int ret = 0;
	if (st->started) {
		ret |= PROBE_STATUS_STARTED | PROBE_STATUS_TARGET_STARTED;
		if (st->out->finished) {
			ret |= PROBE_STATUS_TERMINATED | PROBE_STATUS_TARGET_TERMINATED;
		} else {
			ret |= PROBE_STATUS_RUNNING | PROBE_STATUS_TARGET_RUNNING;
		}
	}
	return ret;
}

static int dummy_start(void *ctx)
{
	struct DummyState *st = (struct DummyState *)ctx;
	int r = pthread_create(&st->probe_thread, NULL, genaddr, st->out);
	if (r == 0) {
		st->started = 1;
		return PROBE_CPFUNC_SUCCESS;
	} else {
		return r;
	}
}

static int dummy_stop(void *ctx)
{
	struct DummyState *st = (struct DummyState *)ctx;
	pthread_cancel(st->probe_thread);
	pthread_join(st->probe_thread, NULL);
	st->out->finished = 1;
	/* Notify output consumer */
	pthread_mutex_lock(&st->out->update_mutex);
	pthread_cond_broadcast(&st->out->update_cond);
	pthread_mutex_unlock(&st->out->update_mutex);

	return PROBE_CPFUNC_SUCCESS;
}

static int dummy_destroy(void *ctx)
{
	free(ctx);
	return PROBE_CPFUNC_SUCCESS;
}


int probe_dummy_setup(struct ProbeOutput *pout, struct ProbeControlPanel *pcp)
{
	if (pout->fmtflags & PROBEOUT_OPSTATS) {
		/* Not supported */
		return 1;
	}
	struct DummyState *st = malloc(sizeof(struct DummyState));
	if (st == NULL) {
		return 1;
	}
	st->started = 0;
	st->out = pout;
	pout->head = 0;
	pout->finished = 0;

	/* Set up control panel funcs */
	{
		probe_cpfunc_t *f = pcp->func;

		f[PROBE_STATUS] = dummy_status;
		f[PROBE_DESTROY] = dummy_destroy;
		f[PROBE_START] = dummy_start;
		f[PROBE_STOP] = dummy_stop;

		f[PROBE_PAUSE] = not_implemented;
		f[PROBE_RESUME] = not_implemented;
		f[PROBE_TARGET_START] = not_implemented;
		f[PROBE_TARGET_STOP] = not_implemented;
		f[PROBE_TARGET_PAUSE] = not_implemented;
		f[PROBE_TARGET_RESUME] = not_implemented;

		pcp->custom = NULL;
		pcp->ctx = st;
	}
	return 0;
}
