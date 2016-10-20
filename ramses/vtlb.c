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

#include <ramses/vtlb.h>

#include "bitops.h"

#include <ramses/vtlbucket.h>
#include <ramses/vtlb_hashtbl.h>
#include <ramses/translate.h>

#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#ifdef RAMSES_VTLB_DEBUG
#include <time.h>
static inline int64_t timedelta(struct timespec t0, struct timespec t)
{
	return ((t.tv_sec - t0.tv_sec) * 1000000000) + (t.tv_nsec - t0.tv_nsec);
}
#endif


struct vtlb_state {
	unsigned int ngen;
	unsigned int actgen;
	unsigned int head;
	unsigned int tail;

	int pagemap_fd;

	int64_t savedtime;
	int64_t gen_age_limit;
	int64_t push_limit;

	struct VTLBucketFuncs bf;

#ifdef RAMSES_VTLB_DEBUG
	uint64_t nhit;
	uint64_t nreq;
	int64_t hittime;
	int64_t misstime;
	int64_t probesum;
#endif

	void *buckets[];
};

static void vtlb_genpop(struct vtlb_state *s)
{
	ramses_vtlbucket_clear(&s->bf, s->buckets[s->tail]);
	if (s->actgen) {
		s->tail = (s->tail + 1) % s->ngen;
		s->actgen--;
	}
}

static void vtlb_genpush(struct vtlb_state *s)
{
	s->head = (s->head + 1) % s->ngen;
	s->actgen++;
	/* Pop and clear bucket if we arrived at tail */
	if (s->actgen == s->ngen) {
		vtlb_genpop(s);
	}
}


static int64_t vtlb_handle_timed(struct vtlb_state *s, int64_t timed)
{
	uint64_t genage = s->gen_age_limit;
	uint64_t agen = s->actgen; /* This updates on pop; need to cache original */
	uint64_t max_trust = s->ngen * genage;
	if (timed > max_trust) {
		/* max_trust exceeded, flush entire vtlb */
		ramses_vtlb_flush(s);
		return timed;
	} else {
		while (timed > max_trust - (agen * genage)) {
			vtlb_genpop(s);
			timed -= genage;
		}
		if (timed > s->push_limit) {
			vtlb_genpush(s);
		}
		return timed;
	}
}

static inline int setup_buckets(struct vtlb_state *s,
                                unsigned int gensize, unsigned int num_gen)
{
	ramses_vtlb_hashtbl_register_funcs(&s->bf);
	for (unsigned int i = 0; i < num_gen; i++) {
		void *b = ramses_vtlb_hashtbl_create(gensize, ramses_hash_twang6432, 64);
		if (b != NULL) {
			s->buckets[i] = b;
		} else {
			for (unsigned int j = 0; j < i; j++) {
				ramses_vtlb_hashtbl_destroy(s->buckets[j]);
			}
			return 1;
		}
	}
	return 0;
}

static inline void *setup_state(unsigned int num_gen,
                                unsigned long min_trust_us,
                                unsigned long max_trust_us,
                                int pagemap_fd)
{
	size_t bufsz = sizeof(struct vtlb_state) + num_gen * sizeof(void *);
	void *buf = malloc(bufsz);
	if (buf != NULL) {
		memset(buf, 0, bufsz);
		struct vtlb_state *s = (struct vtlb_state *)buf;
		s->ngen = num_gen;
		s->pagemap_fd = pagemap_fd;
		s->gen_age_limit = max_trust_us * 1000 / num_gen;
		s->push_limit = min_trust_us * 1000 / num_gen;
	}
	return buf;
}


void *ramses_vtlb_create(unsigned int gensize, unsigned int num_gen,
                         unsigned long min_trust_us, unsigned long max_trust_us,
                         int pagemap_fd)
{
	if (!gensize || !num_gen || !max_trust_us || min_trust_us > max_trust_us) {
		errno = EINVAL;
		return NULL;
	}
	void *buf = setup_state(num_gen, min_trust_us, max_trust_us, pagemap_fd);
	if (buf != NULL) {
		struct vtlb_state *s = (struct vtlb_state *)buf;
		if (setup_buckets(s, gensize, num_gen)) {
			free(buf);
			return NULL;
		}
	}
	return buf;
}

void *ramses_vtlb_create_cust_buckets(struct VTLBucketFuncs *bfuncs,
                                      void *buckets[], unsigned int num_gen,
                                      unsigned long min_trust_us,
                                      unsigned long max_trust_us,
                                      int pagemap_fd)
{
	if (bfuncs == NULL || buckets == NULL || !num_gen || !max_trust_us ||
	    pagemap_fd < 0 || min_trust_us > max_trust_us)
	{
		errno = EINVAL;
		return NULL;
	}
	void *buf = setup_state(num_gen, min_trust_us, max_trust_us, pagemap_fd);
	if (buf != NULL) {
		struct vtlb_state *s = (struct vtlb_state *)buf;
		s->bf = *bfuncs;
		memcpy(s->buckets, buckets, num_gen * sizeof(void *));
	}
	return buf;
}

void ramses_vtlb_destroy(void *vtlb)
{
	struct vtlb_state *s = (struct vtlb_state *)vtlb;
	for (unsigned int i = 0; i < s->ngen; i++) {
		ramses_vtlb_hashtbl_destroy(s->buckets[i]);
	}
	free(vtlb);
}

void ramses_vtlb_update_timedelta(void *vtlb, int64_t timev)
{
	struct vtlb_state *s = (struct vtlb_state *)vtlb;
	if (s->savedtime) {
		timev += s->savedtime;
	}
	int64_t rem = vtlb_handle_timed(s, timev);
	if (rem > s->push_limit) {
		s->savedtime = 0;
	} else {
		s->savedtime = rem;
	}
}

void ramses_vtlb_update_timestamp(void *vtlb, int64_t timev)
{
	struct vtlb_state *s = (struct vtlb_state *)vtlb;
	int64_t timed = timev - s->savedtime;
	if (timed < 0) {
		/* Something's not right; flush */
		ramses_vtlb_flush(s);
		s->savedtime = timev;
	} else if (timed > s->push_limit) {
		vtlb_handle_timed(s, timed);
		s->savedtime = timev;
	}
}

void ramses_vtlb_update(void *vtlb, uint64_t vpfn, physaddr_t pfn)
{
	struct vtlb_state *s = (struct vtlb_state *)vtlb;
	int64_t headhandle;
	ramses_vtlbucket_search(&s->bf, s->buckets[s->head], vpfn, &headhandle);
	ramses_vtlbucket_insert(&s->bf, s->buckets[s->head], vpfn, pfn, headhandle);
}

physaddr_t ramses_vtlb_search(void *vtlb, uint64_t vpfn)
{
	struct vtlb_state *s = (struct vtlb_state *)vtlb;
	int64_t headhandle;
	physaddr_t retval = RAMSES_BADADDR;

	#ifdef RAMSES_VTLB_DEBUG
	struct timespec t0, t;
	s->nreq++;
	clock_gettime(CLOCK_REALTIME, &t0);
	#endif
	if (ramses_vtlbucket_search(&s->bf, s->buckets[s->head], vpfn, &headhandle) == 0) {
		/* Hit in head; best-case scenario */
		retval = ramses_vtlbucket_get(&s->bf, s->buckets[s->head], headhandle);
		#ifdef RAMSES_VTLB_DEBUG
		clock_gettime(CLOCK_REALTIME, &t);
		s->hittime += timedelta(t0, t);
		s->nhit++;
		#endif
	} else {
		/* Check other active generations */
		unsigned int gen = s->head;
		for (unsigned int i = s->actgen; i --> 0;) {
			int64_t handle;
			gen = (gen - 1) % s->ngen;
			if (ramses_vtlbucket_search(&s->bf, s->buckets[gen], vpfn, &handle) == 0) {
				retval = ramses_vtlbucket_get(&s->bf, s->buckets[gen], handle);
				#ifdef RAMSES_VTLB_DEBUG
				clock_gettime(CLOCK_REALTIME, &t);
				s->hittime += timedelta(t0, t);
				s->nhit++;
				s->probesum += handle >> 32;
				#endif
				break;
			}
			#ifdef RAMSES_VTLB_DEBUG
			s->probesum += handle >> 32;
			#endif
		}
		/* Miss in all generations; give up */
	}
	#ifdef RAMSES_VTLB_DEBUG
	s->probesum += headhandle >> 32;
	#endif
	return retval;
}

physaddr_t ramses_vtlb_lookup(void *vtlb, uint64_t vpfn)
{
	struct vtlb_state *s = (struct vtlb_state *)vtlb;
	physaddr_t retval = RAMSES_BADADDR;

	#ifdef RAMSES_VTLB_DEBUG
	struct timespec t0, t;
	clock_gettime(CLOCK_REALTIME, &t0);
	#endif

	retval = ramses_vtlb_search(vtlb, vpfn);
	if (retval == RAMSES_BADADDR) {
		/* Miss in all generations; check pagemap */
		retval = ramses_translate_pagemap(vpfn << 12, s->pagemap_fd);
		if (retval != RAMSES_BADADDR) {
			/* Store in head */
			ramses_vtlb_update(s, vpfn, retval >> 12);
		}
		#ifdef RAMSES_VTLB_DEBUG
		clock_gettime(CLOCK_REALTIME, &t);
		s->misstime += timedelta(t0, t);
		#endif
	}
	return retval;
}

void ramses_vtlb_flush(void *vtlb)
{
	struct vtlb_state *s = (struct vtlb_state *)vtlb;
	while (s->actgen) {
		vtlb_genpop(s);
	}
	vtlb_genpop(s);
}

void ramses_vtlb_update_pagemapfd(void *vtlb, int pagemap_fd)
{
	struct vtlb_state *s = (struct vtlb_state *)vtlb;
	close(s->pagemap_fd);
	s->pagemap_fd = pagemap_fd;
}

#ifdef RAMSES_VTLB_DEBUG
float ramses_vtlb_hitrate(void *vtlb)
{
	struct vtlb_state *s = (struct vtlb_state *)vtlb;
	return (float)s->nhit / (float)s->nreq;
}
double ramses_vtlb_avg_hit_time(void *vtlb)
{
	struct vtlb_state *s = (struct vtlb_state *)vtlb;
	return (double)s->hittime / (double)s->nhit;
}
double ramses_vtlb_avg_miss_time(void *vtlb)
{
	struct vtlb_state *s = (struct vtlb_state *)vtlb;
	return (double)s->misstime / (double)(s->nreq - s->nhit);
}
float ramses_vtlb_avg_probe(void *vtlb)
{
	struct vtlb_state *s = (struct vtlb_state *)vtlb;
	return (float)s->probesum / (float)s->nreq;
}
uint64_t ramses_vtlb_get_nreqs(void *vtlb)
{
	struct vtlb_state *s = (struct vtlb_state *)vtlb;
	return s->nreq;
}
void ramses_vtlb_clear_stats(void *vtlb)
{
	struct vtlb_state *s = (struct vtlb_state *)vtlb;
	s->nhit = 0;
	s->nreq = 0;
	s->probesum = 0;
	s->hittime = 0;
	s->misstime = 0;
}
#endif
