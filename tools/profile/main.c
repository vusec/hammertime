/*
 * Copyright (c) 2016 Andrei Tatar
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
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

#define _GNU_SOURCE

#include "addr.h"
#include "hammer.h"

#include <ramses/util.h>

#include <assert.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

#include <sys/mman.h>
/* Sort-of HACK; sys/mman.h should have this though */
#ifndef MAP_HUGE_1GB
#include <linux/mman.h>
#endif

#define STRTOK_BUFSIZE 128

#define V_ERR 0
#define V_INFO 1
#define V_DEBUG 2
static int VERBOSITY = V_ERR;

#define DRAM_REFRESH 64000
#define DRAM_OVRSHOOT 6000

/* Conservative estimate */
#define CACHE_LINE_SIZE 64

static const uint8_t DEFAULT_FILL_PATTERN[] = {0xff};
static const size_t DEFAULT_FILL_PATLEN = 1;
static const uint8_t DEFAULT_TFILL_PATTERN[] = {0x00};
static const size_t DEFAULT_TFILL_PATLEN = 1;

/* Pressure thread */

/* Default minimum to cover 8-bank DDR3 */
static size_t PRESSURE_BUF_LEN = 1 << 16;
static int PRESSURE_GRAN_SHIFT = 0;

static void *rand_pressure_thread(void *buf)
{
	size_t bufmask = PRESSURE_BUF_LEN - 1;
	size_t gransh = PRESSURE_GRAN_SHIFT;
	int8_t *cbuf = buf;

	long int i = 0;
	for (;;i = random()) {
		volatile int8_t *p = cbuf + ((i << gransh) & bufmask);
		asm volatile ("clflush (%0)\n\t" : : "r" (p) : "memory");
		*p;
	}
	return NULL;
}

static void *seq_pressure_thread(void *buf)
{
	size_t bufmask = PRESSURE_BUF_LEN - 1;
	size_t gran = 1 << PRESSURE_GRAN_SHIFT;
	volatile int8_t *cbuf = buf;

	for (size_t i = 0; ; i = (i + gran) & bufmask) {
		asm volatile ("clflush (%0)\n\t" : : "r" (cbuf + i) : "memory");
		*(cbuf + i);
	}
	return NULL;
}

/* Hammer Context */

struct HammerCtx {
	struct AddrEntry *targets;
	size_t tcount;
	const void *pat;
	const void *alt_pat;
	size_t patlen;
	const void *tpat;
	const void *alt_tpat;
	size_t tpatlen;
	int hammer_opts;
	int calibration_mult;
	int window_rad;
	int target_dist;
	struct AddrEntry *ap_targets;
	size_t ap_tcount;
	FILE *outf;
};

#define HAM_OPT_DUALCHANNEL    0b000001
#define HAM_OPT_SINGLE_SIDED   0b000010
#define HAM_OPT_DOUBLE_SIDED   0b000100
#define HAM_OPT_AUTOPRESSURE   0b001000
#define HAM_OPT_DRY_RUN        0b010000
#define HAM_OPT_ALTPAT         0b100000

/* Hammer target queue */

typedef int16_t hamqueue_off_t;

struct HamQueue {
	size_t *q;
	hamqueue_off_t len;
	hamqueue_off_t count;
	hamqueue_off_t head;
};

static void hamqueue_push(struct HamQueue *q, size_t v)
{
	if (q->count == q->len) {
		q->q[q->head] = v;
		q->head = (q->head + 1) % q->len;
	} else {
		q->q[(q->head + q->count) % q->len] = v;
		q->count++;
	}
}

static size_t hamqueue_idx(struct HamQueue *q, hamqueue_off_t i)
{
	if (i < 0) i += q->count;
	return q->q[(q->head + i) % q->len];
}

static void hamqueue_clear(struct HamQueue *q)
{
	q->head = (q->head + q->count - 1) % q->len;
	q->count = 1;
}

/* Util functions */

/* Find a target entry in pool that is in bank conflict with loc
 * Return pointer to found target or NULL if no conflict found
 */
static struct AddrEntry *find_bank_conflict(struct DRAMAddr loc, struct AddrEntry *pool, size_t poolsize)
{
	for (size_t i = 0; i < poolsize; i++) {
		if (ramses_same_bank(pool[i].dramaddr, loc)) {
			return &pool[i];
		}
	}
	return NULL;
}

static void print_entry(FILE *f, struct AddrEntry ent, const char *trail)
{
	struct DRAMAddr da = ent.dramaddr;
	if (VERBOSITY & V_DEBUG) {
		fprintf(f, "%lx %lx (%1x %1x %1x %1x %4x) ", ent.virtp, ent.len,
											 da.chan, da.dimm,  da.rank, da.bank, da.row);
	} else {
		fprintf(f, "(%1x %1x %1x %1x %4x) ", da.chan, da.dimm, da.rank, da.bank, da.row);
	}
	if (trail) {
		fputs(trail, f);
	}
}

static void flush_entry(struct AddrEntry ent)
{
	for (size_t i = 0; i < ent.len; i += CACHE_LINE_SIZE) {
		asm volatile ("clflush (%0)\n\t" : : "r" ((char *)ent.virtp + i) : "memory");
	}
}

static void check_entry(struct HammerCtx *ctx, struct AddrEntry ent, bool is_target)
{
	size_t idx = 0;
	bool ent_header_printed = false;
	const void *pat = (is_target) ? ctx->tpat : ctx->pat;
	size_t patlen = (is_target) ? ctx->tpatlen : ctx->patlen;
	do {
		idx = check((void *)(ent.virtp), ent.len, pat, patlen, idx);
		if (idx < ent.len) {
			if (!ent_header_printed) {
				print_entry(ctx->outf, ent, NULL);
				ent_header_printed = true;
			}
			fprintf(ctx->outf,
			        "%04lx|%02x|%02x ",
			        (ent.dramaddr.col << 3) + idx,
			        ((uint8_t *)ent.virtp)[idx],
			        ((uint8_t *)ctx->pat)[idx % ctx->patlen]);
			idx++;
		}
	} while (idx < ent.len);
}

static size_t next_row_entry_idx(struct HammerCtx *ctx, size_t startidx)
{
	size_t ret;
	for (ret = startidx;
		 ret < ctx->tcount && ramses_same_row(ctx->targets[ret].dramaddr, ctx->targets[startidx].dramaddr);
		 ret++);
	return ret;
}

/* Try to find an autopressure target that produces a bank-conflict with ht
 * If that fails, return an arbitrary pressure buffer location */
static uintptr_t ap_bank_conflict(struct HammerCtx *ctx, struct AddrEntry ht)
{
	struct AddrEntry *apt = find_bank_conflict(ht.dramaddr, ctx->ap_targets, ctx->ap_tcount);
	if (apt != NULL) {
		return apt->virtp;
	} else {
		return ctx->ap_targets[0].virtp;
	}
}

/* Attempt to fill a HamQueue with consecutive row targets.
 * Updates idx as it progresses.
 * If end is 0, ctx->tcount is used.
 * Returns when queue is full or idx has either reached the end or a non-consecutive row.
 * Assumes the queue is already "primed" with 1 element.
 */
static void fill_queue(struct HammerCtx *ctx, struct HamQueue *q, size_t *idx, size_t end)
{
	if (!end) {
		end = ctx->tcount;
	}
	do {
		hamqueue_push(q, *idx);
		*idx = next_row_entry_idx(ctx, *idx);
	} while (q->count < q->len &&
			 hamqueue_idx(q, -1) < end &&
			 ramses_succ_rows(ctx->targets[hamqueue_idx(q, -2)].dramaddr, ctx->targets[hamqueue_idx(q, -1)].dramaddr));
}

/* Attempt to "step", i.e. push one row into a full queue (throwing out the head).
 * Updates idx if it progresses.
 * If end is 0, ctx->tcount is used.
 * Fails if upcoming row is not the direct succesor to the last row in queue.
 * Returns true if step has occured, false otherwise.
 */
static bool step_queue(struct HammerCtx *ctx, struct HamQueue *q, size_t *idx, size_t end)
{
	bool can_step;
	if (!end) {
		end = ctx->tcount;
	}
	can_step = (hamqueue_idx(q, -1) < end &&
				ramses_succ_rows(ctx->targets[hamqueue_idx(q, -2)].dramaddr, ctx->targets[hamqueue_idx(q, -1)].dramaddr));
	if (can_step) {
		hamqueue_push(q, *idx);
		*idx = next_row_entry_idx(ctx, *idx);
	}
	return can_step;
}

/* Get a HamQueue ready for hammering (i.e. filled with consecutive rows).
 * This implies a (possible) combination of stepping, filling and clearing.
 * Updates idx as it progresses.
 * If end is 0, ctx->tcount is used.
 * Returns false if idx has reached end, true otherwise.
 * Assumes the queue is already "primed" with 1 element.
 */
static bool ready_queue(struct HammerCtx *ctx, struct HamQueue *q, size_t *idx, size_t end)
{
	if (!end) {
		end = ctx->tcount;
	}
	/* Attempt to step; if successful, return */
	if (q->count == q->len) {
		if (step_queue(ctx, q, idx, end)) {
			return true;
		}
	}
	/* Step didn't (or couldn't) work; clear and refill */
	do {
		hamqueue_clear(q);
		fill_queue(ctx, q, idx, end);
		if (q->count == q->len) {
			return true;
		}
	} while (*idx < end);
	/* Target list exhausted; game over */
	return false;
}

/* Slightly less of a HACK ; only used by the rh_* functions */
#define FOR_ENTI(q,i) for (size_t enti = hamqueue_idx(q, i); enti < hamqueue_idx(q, (i) + 1); enti++)
#define FILL_SHORTHAND(c,i) \
fill((void *)((c)->targets[i].virtp), (c)->targets[i].len, (c)->pat, (c)->patlen)
#define TFILL_SHORTHAND(c,i) \
fill((void *)((c)->targets[i].virtp), (c)->targets[i].len, (c)->tpat, (c)->tpatlen)

/* rh_* helper fuctions for the rowhammer_* algorithms */
static void rh_fill_singlesided(struct HammerCtx *ctx, struct HamQueue *q)
{
	hamqueue_off_t mid = (q->count - 1) / 2;
	for (hamqueue_off_t i = 0; i < mid; i++) {
		FOR_ENTI(q, i) {
			FILL_SHORTHAND(ctx, enti);
			flush_entry(ctx->targets[enti]);
		}
		FOR_ENTI(q, -i - 2) {
			FILL_SHORTHAND(ctx, enti);
			flush_entry(ctx->targets[enti]);
		}
	}
}

static void rh_check_singlesided(struct HammerCtx *ctx, struct HamQueue *q)
{
	hamqueue_off_t mid = (q->count - 1) / 2;
	for (hamqueue_off_t i = 0; i < mid; i++) {
		FOR_ENTI(q, i) {
			flush_entry(ctx->targets[enti]);
			check_entry(ctx, ctx->targets[enti], false);
			flush_entry(ctx->targets[enti]);
		}
		FOR_ENTI(q, -i - 2) {
			flush_entry(ctx->targets[enti]);
			check_entry(ctx, ctx->targets[enti], false);
			flush_entry(ctx->targets[enti]);
		}
	}
}

static void rh_fill_doublesided(struct HammerCtx *ctx, struct HamQueue *q, hamqueue_off_t t1, hamqueue_off_t t2)
{
	for (hamqueue_off_t i = 0; i < t1; i++) {
		FOR_ENTI(q, i) {
			FILL_SHORTHAND(ctx, enti);
			flush_entry(ctx->targets[enti]);
		}
	}
	for (hamqueue_off_t i = t1 + 1; i < t2; i++) {
		FOR_ENTI(q, i) {
			FILL_SHORTHAND(ctx, enti);
			flush_entry(ctx->targets[enti]);
		}
	}
	for (hamqueue_off_t i = t2 + 1; i < q->count - 1; i++) {
		FOR_ENTI(q, i) {
			FILL_SHORTHAND(ctx, enti);
			flush_entry(ctx->targets[enti]);
		}
	}
	FOR_ENTI(q, t1) {
		TFILL_SHORTHAND(ctx, enti);
		flush_entry(ctx->targets[enti]);
	}
	FOR_ENTI(q, t2) {
		TFILL_SHORTHAND(ctx, enti);
		flush_entry(ctx->targets[enti]);
	}
}

static void rh_check_doublesided(struct HammerCtx *ctx, struct HamQueue *q, hamqueue_off_t t1, hamqueue_off_t t2)
{
	for (hamqueue_off_t i = 0; i < t1; i++) {
		FOR_ENTI(q, i) {
			flush_entry(ctx->targets[enti]);
			check_entry(ctx, ctx->targets[enti], false);
		}
	}
	for (hamqueue_off_t i = t1 + 1; i < t2; i++) {
		FOR_ENTI(q, i) {
			flush_entry(ctx->targets[enti]);
			check_entry(ctx, ctx->targets[enti], false);
		}
	}
	for (hamqueue_off_t i = t2 + 1; i < q->count - 1; i++) {
		FOR_ENTI(q, i) {
			flush_entry(ctx->targets[enti]);
			check_entry(ctx, ctx->targets[enti], false);
		}
	}
	/* Sanity check */
	FOR_ENTI(q, t1) {
		flush_entry(ctx->targets[enti]);
		check_entry(ctx, ctx->targets[enti], true);
	}
	FOR_ENTI(q, t2) {
		flush_entry(ctx->targets[enti]);
		check_entry(ctx, ctx->targets[enti], true);
	}
}

#undef FOR_ENTI
#undef FILL_SHORTHAND
#undef TFILL_SHORTHAND

/* Actual hammering functions operating on HamQueues.
 * The last index is used as iteration boundary (i.e. "end of last row in queue") and never
 * dereferenced. Therefore it is safe to have ctx->tcount as last element in queue.
 */

/* Hammers the central row in a queue, fills and checks all others; assumes queue is full */
static void rowhammer_singlechannel_singlesided(struct HammerCtx *ctx, struct HamQueue *q, unsigned int iters)
{
	assert(q->len == q->count);

	hamqueue_off_t mid = (q->count - 1) / 2;
	struct AddrEntry ht = ctx->targets[hamqueue_idx(q, mid)];

	rh_fill_singlesided(ctx, q);

	if (ctx->hammer_opts & HAM_OPT_AUTOPRESSURE) {
		uintptr_t htargs[] = {
			ht.virtp,
			ap_bank_conflict(ctx, ht)
		};
		hammer_double(htargs, iters);
	} else {
		uintptr_t htargs[] = {ht.virtp};
		hammer_single(htargs, iters);
	}

	print_entry(ctx->outf, ht, ": ");
	rh_check_singlesided(ctx, q);
	putc('\n', ctx->outf);
}

/* Hammers the direct neighbours of the central row, fills and checks all others; assumes queue is full */
static void rowhammer_singlechannel_doublesided(struct HammerCtx *ctx, struct HamQueue *q, unsigned int iters)
{
	assert(q->len == q->count);

	hamqueue_off_t mid = (q->count - 1) / 2;
	hamqueue_off_t t1 = mid - (ctx->target_dist / 2) - (ctx->target_dist % 2);
	hamqueue_off_t t2 = t1 + ctx->target_dist + 1;

	struct AddrEntry ht1 = ctx->targets[hamqueue_idx(q, t1)];
	struct AddrEntry ht2 = ctx->targets[hamqueue_idx(q, t2)];

	rh_fill_doublesided(ctx, q, t1, t2);

	if (ctx->hammer_opts & HAM_OPT_AUTOPRESSURE) {
		uintptr_t htargs[] = {
			ht1.virtp,
			ht2.virtp,
			ap_bank_conflict(ctx, ht1)
		};
		hammer_triple(htargs, iters);
	} else {
		uintptr_t htargs[] = {ht1.virtp, ht2.virtp};
		hammer_double(htargs, iters);
	}

	print_entry(ctx->outf, ht1, NULL);
	print_entry(ctx->outf, ht2, ": ");
	rh_check_doublesided(ctx, q, t1, t2);
	putc('\n', ctx->outf);
}

/* Same as above, optimized for dual-channel memory */
static void rowhammer_dualchannel_singlesided(struct HammerCtx *ctx, struct HamQueue *q1, struct HamQueue *q2, unsigned int iters)
{
	assert(q1->len == q1->count);
	assert(q2->len == q2->count);

	hamqueue_off_t m1 = (q1->count - 1) / 2;
	hamqueue_off_t m2 = (q2->count - 1) / 2;
	struct AddrEntry ht1 = ctx->targets[hamqueue_idx(q1, m1)];
	struct AddrEntry ht2 = ctx->targets[hamqueue_idx(q2, m2)];

	rh_fill_singlesided(ctx, q1);
	rh_fill_singlesided(ctx, q2);

	if (ctx->hammer_opts & HAM_OPT_AUTOPRESSURE) {
		uintptr_t htargs[] = {
			ht1.virtp,
			ap_bank_conflict(ctx, ht1),
			ht2.virtp,
			ap_bank_conflict(ctx, ht2)
		};
		hammer_quad(htargs, iters);
	} else {
		uintptr_t htargs[] = {ht1.virtp, ht2.virtp};
		hammer_double(htargs, iters);
	}

	print_entry(ctx->outf, ht1, ": ");
	rh_check_singlesided(ctx, q1);
	putc('\n', ctx->outf);
	print_entry(ctx->outf, ht2, ": ");
	rh_check_singlesided(ctx, q2);
	putc('\n', ctx->outf);
}

/* Ditto */
static void rowhammer_dualchannel_doublesided(struct HammerCtx *ctx, struct HamQueue *q1, struct HamQueue *q2,  unsigned int iters)
{
	assert(q1->len == q1->count);
	assert(q2->len == q2->count);

	hamqueue_off_t m1 = (q1->count - 1) / 2;
	hamqueue_off_t m2 = (q2->count - 1) / 2;
	hamqueue_off_t t11 = m1 - (ctx->target_dist / 2) - (ctx->target_dist % 2);
	hamqueue_off_t t12 = t11 + ctx->target_dist + 1;
	hamqueue_off_t t21 = m2 - (ctx->target_dist / 2) - (ctx->target_dist % 2);
	hamqueue_off_t t22 = t21 + ctx->target_dist + 1;

	struct AddrEntry ht11 = ctx->targets[hamqueue_idx(q1, t11)];
	struct AddrEntry ht12 = ctx->targets[hamqueue_idx(q1, t12)];
	struct AddrEntry ht21 = ctx->targets[hamqueue_idx(q2, t21)];
	struct AddrEntry ht22 = ctx->targets[hamqueue_idx(q2, t22)];

	rh_fill_doublesided(ctx, q1, t11, t12);
	rh_fill_doublesided(ctx, q2, t21, t22);

	if (ctx->hammer_opts & HAM_OPT_AUTOPRESSURE) {
		uintptr_t htargs[] = {
			ht11.virtp,
			ht12.virtp,
			ap_bank_conflict(ctx, ht11),
			ht21.virtp,
			ht22.virtp,
			ap_bank_conflict(ctx, ht21)
		};
		hammer_six(htargs, iters);
	} else {
		uintptr_t htargs[] = {
			ht11.virtp,
			ht12.virtp,
			ht21.virtp,
			ht22.virtp
		};
		hammer_quad(htargs, iters);
	}

	print_entry(ctx->outf, ht11, NULL);
	print_entry(ctx->outf, ht12, ": ");
	rh_check_doublesided(ctx, q1, t11, t12);
	putc('\n', ctx->outf);
	print_entry(ctx->outf, ht21, NULL);
	print_entry(ctx->outf, ht22, ": ");
	rh_check_doublesided(ctx, q2, t21, t22);
	putc('\n', ctx->outf);
}


/* Main profiling loop */
static int run_profile(struct HammerCtx *ctx)
{
	unsigned int single_cal = 0;
	unsigned int double_cal = 0;
	struct AddrEntry *t = ctx->targets;
	size_t idx1, idx2, boundary;
	idx1 = idx2 = boundary = ctx->tcount;

	bool dualchan = (ctx->targets[0].dramaddr.chan != ctx->targets[ctx->tcount - 1].dramaddr.chan &&
					 (ctx->hammer_opts & HAM_OPT_DUALCHANNEL));

	struct HamQueue *q1, *q2 = NULL;

	q1 = alloca(sizeof(*q1));
	if (dualchan) {
		q2 = alloca(sizeof(*q2));
	}

	q1->len = ctx->window_rad * 2 + 2;
	q1->count = 0;
	q1->head = 0;
	q1->q = alloca(sizeof(*(q1->q)) * q1->len);
	if (dualchan) {
		q2->len = ctx->window_rad * 2 + 2;
		q2->count = 0;
		q2->head = 0;
		q2->q = alloca(sizeof(*(q2->q)) * q2->len);
	}

	idx1 = next_row_entry_idx(ctx, 0);
	if (dualchan) {
		/* Find channel boundary */
		for (boundary = ctx->tcount / 2;
			 ctx->targets[boundary].dramaddr.chan != ctx->targets[0].dramaddr.chan;
			 boundary--);
		boundary = next_row_entry_idx(ctx, boundary);
		idx2 = next_row_entry_idx(ctx, boundary);
	}

	if (ctx->hammer_opts & HAM_OPT_SINGLE_SIDED) {
		if (VERBOSITY & V_INFO) {
			fputs("Calibrating single hammer... ", stderr);
		}
		if (dualchan) {
			if (ctx->hammer_opts & HAM_OPT_AUTOPRESSURE) {
				uintptr_t htargs[] = {
					t[0].virtp, ap_bank_conflict(ctx, t[0]),
					t[boundary].virtp, ap_bank_conflict(ctx, t[boundary])
				};
				single_cal = calibrate_hammer(&hammer_quad, htargs, DRAM_REFRESH, DRAM_OVRSHOOT);
			} else {
				uintptr_t htargs[] = { t[0].virtp, t[boundary].virtp };
				single_cal = calibrate_hammer(&hammer_double, htargs, DRAM_REFRESH, DRAM_OVRSHOOT);
			}
		} else {
			if (ctx->hammer_opts & HAM_OPT_AUTOPRESSURE) {
				uintptr_t htargs[] = { t[0].virtp, ap_bank_conflict(ctx, t[0]) };
				single_cal = calibrate_hammer(&hammer_double, htargs, DRAM_REFRESH, DRAM_OVRSHOOT);
			} else {
				uintptr_t htargs[] = { t[0].virtp };
				single_cal = calibrate_hammer(&hammer_single, htargs, DRAM_REFRESH, DRAM_OVRSHOOT);
			}
		}
		if (VERBOSITY & V_INFO) {
			fprintf(stderr, "%d reads per refresh interval\n", single_cal);
		}
		single_cal *= ctx->calibration_mult;
	}
	if (ctx->hammer_opts & HAM_OPT_DOUBLE_SIDED) {
		if (VERBOSITY & V_INFO) {
			fputs("Calibrating double hammer... ", stderr);
		}
		if (dualchan) {
			if (ctx->hammer_opts & HAM_OPT_AUTOPRESSURE) {
				uintptr_t htargs[] = {
					t[0].virtp, t[idx1].virtp, ap_bank_conflict(ctx, t[0]),
					t[boundary].virtp, t[idx2].virtp, ap_bank_conflict(ctx, t[boundary])
				};
				double_cal = calibrate_hammer(&hammer_six, htargs, DRAM_REFRESH, DRAM_OVRSHOOT);
			} else {
				uintptr_t htargs[] = {
					t[0].virtp, t[idx1].virtp,
					t[boundary].virtp, t[idx2].virtp
				};
				double_cal = calibrate_hammer(&hammer_quad, htargs, DRAM_REFRESH, DRAM_OVRSHOOT);
			}
		} else {
			if (ctx->hammer_opts & HAM_OPT_AUTOPRESSURE) {
				uintptr_t htargs[] = { t[0].virtp, t[idx1].virtp, ap_bank_conflict(ctx, t[0]) };
				double_cal = calibrate_hammer(&hammer_triple, htargs, DRAM_REFRESH, DRAM_OVRSHOOT);
			} else {
				uintptr_t htargs[] = { t[0].virtp, t[idx1].virtp };
				double_cal = calibrate_hammer(&hammer_double, htargs, DRAM_REFRESH, DRAM_OVRSHOOT);
			}
		}
		if (VERBOSITY & V_INFO) {
			fprintf(stderr, "%d reads per refresh interval\n", double_cal);
		}
		double_cal *= ctx->calibration_mult;
	}

	if (ctx->hammer_opts & HAM_OPT_DRY_RUN) {
		if (VERBOSITY & V_DEBUG) {
			for (size_t i = 0; i < ctx->tcount; i++) {
				print_entry(ctx->outf, t[i], "\n");
			}
		}
		return 0;
	}

	hamqueue_push(q1, 0);
	if (dualchan) {
		hamqueue_push(q2, boundary);
		while (ready_queue(ctx, q1, &idx1, boundary) && ready_queue(ctx, q2, &idx2, 0)) {
			if (ctx->hammer_opts & HAM_OPT_SINGLE_SIDED) {
				rowhammer_dualchannel_singlesided(ctx, q1, q2, single_cal);
				if (ctx->hammer_opts & HAM_OPT_ALTPAT) {
					const void *tmppat = ctx->pat;
					const void *tmptpat = ctx->tpat;
					ctx->pat = ctx->alt_pat;
					ctx->tpat = ctx->alt_tpat;
					rowhammer_dualchannel_singlesided(ctx, q1, q2, single_cal);
					ctx->pat = tmppat;
					ctx->tpat = tmptpat;
				}
			}
			if (ctx->hammer_opts & HAM_OPT_DOUBLE_SIDED) {
				rowhammer_dualchannel_doublesided(ctx, q1, q2, double_cal);
				if (ctx->hammer_opts & HAM_OPT_ALTPAT) {
					const void *tmppat = ctx->pat;
					const void *tmptpat = ctx->tpat;
					ctx->pat = ctx->alt_pat;
					ctx->tpat = ctx->alt_tpat;
					rowhammer_dualchannel_doublesided(ctx, q1, q2, double_cal);
					ctx->pat = tmppat;
					ctx->tpat = tmptpat;
				}
			}
		}
	} else {
		while (ready_queue(ctx, q1, &idx1, 0)) {
			if (ctx->hammer_opts & HAM_OPT_SINGLE_SIDED) {
				rowhammer_singlechannel_singlesided(ctx, q1, single_cal);
				if (ctx->hammer_opts & HAM_OPT_ALTPAT) {
					const void *tmppat = ctx->pat;
					const void *tmptpat = ctx->tpat;
					ctx->pat = ctx->alt_pat;
					ctx->tpat = ctx->alt_tpat;
					rowhammer_singlechannel_singlesided(ctx, q1, single_cal);
					ctx->pat = tmppat;
					ctx->tpat = tmptpat;
				}
			}
			if (ctx->hammer_opts & HAM_OPT_DOUBLE_SIDED) {
				rowhammer_singlechannel_doublesided(ctx, q1, double_cal);
				if (ctx->hammer_opts & HAM_OPT_ALTPAT) {
					const void *tmppat = ctx->pat;
					const void *tmptpat = ctx->tpat;
					ctx->pat = ctx->alt_pat;
					ctx->tpat = ctx->alt_tpat;
					rowhammer_singlechannel_doublesided(ctx, q1, double_cal);
					ctx->pat = tmppat;
					ctx->tpat = tmptpat;
				}
			}
		}
	}

	return 0;
}


static int str2pat(const char *str, const void **pat, size_t *patlen)
{
	char *endp = NULL;
	char tmp[3];
	tmp[2] = '\0';
	size_t len = strlen(str);
	if (len % 2) {
		return EINVAL;
	}
	len /= 2;

	*pat = malloc(len);
	for (size_t i = 0; i < len; i++) {
		tmp[0] = str[2*i];
		tmp[1] = str[2*i + 1];
		errno = 0;
		((uint8_t *)*pat)[i] = (uint8_t) strtol(tmp, &endp, 16);
		if (errno) {
			return errno;
		}
		if (*endp != '\0') {
			return EINVAL;
		}
	}
	*patlen = len;
	return 0;
}

static int suffix2shift(char suffix)
{
	int shift = 0;
	switch (suffix) {
		case 't':
		case 'T':
			shift += 10;
		case 'g':
		case 'G':
			shift += 10;
		case 'm':
		case 'M':
			shift += 10;
		case 'k':
		case 'K':
			shift += 10;
			break;
	}
	return shift;
}

int main(int argc, char *argv[])
{
	int retval;

	void *buf;
	size_t bufsize = 0;
	struct MemorySystem msys;

	int mem_pressure = 0;
	int mmap_flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE;
	void *(*pressure_func)(void *) = rand_pressure_thread;
	int gran_override = -1;
	bool autopres = false;

	const int DEFAULT_HAMMER_OPTS = HAM_OPT_DOUBLE_SIDED;
	/* Setup context defaults */
	struct HammerCtx ctx;
	ctx.pat = DEFAULT_FILL_PATTERN;
	ctx.alt_pat = NULL;
	ctx.patlen = DEFAULT_FILL_PATLEN;
	ctx.tpat = DEFAULT_TFILL_PATTERN;
	ctx.alt_tpat = NULL;
	ctx.tpatlen = DEFAULT_TFILL_PATLEN;
	ctx.calibration_mult = 3;
	ctx.window_rad = 1;
	ctx.target_dist = 1;
	ctx.hammer_opts = DEFAULT_HAMMER_OPTS;
	ctx.ap_targets = NULL;
	ctx.ap_tcount = 0;
	ctx.outf = stdout;

	int opt;
	while ((opt = getopt(argc, argv, "hvdpwalHis:O:W:I:D:g:P:T:")) != -1) {
		switch (opt) {
			case 's':
			{
				char *end = NULL;
				size_t last = strlen(optarg) - 1;
				errno = 0;
				bufsize = strtoll(optarg, &end, 0) << suffix2shift(optarg[last]);
				if (errno || end < &optarg[last] || end == optarg) {
					perror("Invalid buffer size");
					goto err_usage;
				}
			}
				break;
			case 'O':
				ctx.hammer_opts &= ~DEFAULT_HAMMER_OPTS;
				for (char *c = optarg; *c != '\0'; c++)
					switch (*c) {
					case '2':
						ctx.hammer_opts |= HAM_OPT_DUALCHANNEL;
						break;
					case 's':
						ctx.hammer_opts |= HAM_OPT_SINGLE_SIDED;
						break;
					case 'd':
						ctx.hammer_opts |= HAM_OPT_DOUBLE_SIDED;
						break;
					default:
						fprintf(stderr, "Unrecognized hammer option: %c\n", *c);
						goto err_usage;
					}
				break;
			case 'W':
			{
				char *end = NULL;
				errno = 0;
				ctx.window_rad = strtol(optarg, &end, 0);
				if (errno || end == optarg) {
					perror("Invalid hammer window radius");
					goto err_usage;
				}
				if (ctx.window_rad < 1) {
					fputs("Invalid hammer window radius: value must be greater than or equal to 1\n", stderr);
					goto err_usage;
				}
			}
				break;
			case 'I':
			{
				char *end = NULL;
				errno = 0;
				ctx.calibration_mult = strtol(optarg, &end, 0);
				if (errno || end == optarg) {
					perror("Invalid hammer interval multiplier");
					goto err_usage;
				}
				if (ctx.calibration_mult < 1) {
					fputs("Invalid hammer interval multiplier: value must be greater than or equal to 1\n", stderr);
					goto err_usage;
				}
			}
				break;
			case 'D':
			{
				char *end = NULL;
				errno = 0;
				ctx.target_dist = strtol(optarg, &end, 0);
				if (errno || end == optarg) {
					perror("Invalid target distance");
					goto err_usage;
				}
				if (ctx.calibration_mult < 0) {
					fputs("Invalid target distance: value must be greater than or equal to 0\n", stderr);
					goto err_usage;
				}
			}
				break;
			case 'g':
			{
				char *end = NULL;
				errno = 0;
				gran_override = strtol(optarg, &end, 0);
				if (errno || end == optarg) {
					perror("Invalid pressure granularity shift");
					goto err_usage;
				}
			}
				break;
			case 'P':
				if (str2pat(optarg, &ctx.pat, &ctx.patlen)) {
					fprintf(stderr, "Invalid victim fill pattern: %s\n", optarg);
					goto err_usage;
				}
				break;
			case 'T':
				if (str2pat(optarg, &ctx.tpat, &ctx.tpatlen)) {
					fprintf(stderr, "Invalid target fill pattern: %s\n", optarg);
					goto err_usage;
				}
				break;
			case 'l':
				mmap_flags |= MAP_LOCKED;
				break;
			case 'H':
				if (mmap_flags & MAP_HUGETLB) {
					mmap_flags |= MAP_HUGE_1GB;
				} else {
					mmap_flags |= MAP_HUGETLB;
				}
				break;
			case 'p':
				mem_pressure++;
				break;
			case 'w':
				pressure_func = seq_pressure_thread;
				break;
			case 'a':
				autopres = true;
				break;
			case 'v':
				VERBOSITY = (VERBOSITY << 1) + 1;
				break;
			case 'd':
				ctx.hammer_opts |= HAM_OPT_DRY_RUN;
				break;
			case 'i':
				ctx.hammer_opts |= HAM_OPT_ALTPAT;
				ctx.alt_pat = malloc(ctx.patlen);
				ctx.alt_tpat = malloc(ctx.tpatlen);
				for (size_t i = 0; i < ctx.patlen; i++) {
					((uint8_t *)ctx.alt_pat)[i] = ~(((uint8_t *)ctx.pat)[i]);
				}
				for (size_t i = 0; i < ctx.tpatlen; i++) {
					((uint8_t *)ctx.alt_tpat)[i] = ~(((uint8_t *)ctx.tpat)[i]);
				}
				break;
			case 'h':
				fputs("Hammertime profiler --- a geometry-aware rowhammer probing tool\n", stderr);
			default:
				goto err_usage;
		}
	}

	if (bufsize == 0) {
		fprintf(stderr, "Buffer size not specified\n");
		goto err_usage;
	}
	if (ctx.target_dist >= ctx.window_rad * 2) {
		fprintf(stderr,
				"Target distance too large. Can be at most %d for current window radius %d\n",
				ctx.window_rad * 2 - 1, ctx.window_rad);
		goto err_usage;
	}

	if (optind + 1 < argc) {
		if ((ctx.outf = fopen(argv[optind + 1], "w")) == NULL) {
			perror("Error opening output file");
			goto err_usage;
		}
	}
	if (optind < argc) {
		FILE *msysf;
		if (strcmp("-", argv[optind]) == 0) {
			msysf = stdin;
		} else {
			if ((msysf = fopen(argv[optind], "r")) == NULL) {
				perror("Error opening msys file");
				goto err_usage;
			}
		}
		if (ramses_memsys_load_file(msysf, &msys, stderr)) {
			fprintf(stderr, "Error loading msys\n");
			goto err_usage;
		}
	} else {
		fprintf(stderr, "Missing argument: MSYS_FILE\n");
		goto err_usage;
	}

	if (VERBOSITY & V_DEBUG) {
		fprintf(stderr, "Allocating buffer (size 0x%lx)\n", bufsize);
	}
	buf = mmap(NULL, bufsize, PROT_READ | PROT_WRITE, mmap_flags, -1, 0);
	if (buf == MAP_FAILED) {
		perror("Buffer allocation failed");
		return 2;
	}

	if (VERBOSITY & V_INFO) {
		fputs("Setting up targets... ", stderr);
	}
	ctx.tcount = setup_targets(&(ctx.targets), buf, bufsize, &msys);
	if (VERBOSITY & V_INFO) {
		fputs("done\n", stderr);
	}
	if (VERBOSITY & V_DEBUG) {
		fprintf(stderr, "Target count: %ld\n", ctx.tcount);
	}

	if (mem_pressure || autopres) {
		int press_mmap_flags = mmap_flags & ~MAP_HUGE_1GB;
		size_t pbuf_len = PRESSURE_BUF_LEN;

		if (!(press_mmap_flags & MAP_HUGETLB)) {
			/* If no hugepages are available, we need larger pressure buffers
			 * to more likely have all combinations of channel/dimm/rank/bank addresses */
			pbuf_len <<= 3;
		}
		if (msys.mem_geometry & MEMGEOM_RANKSELECT) pbuf_len <<= 1;
		if (msys.mem_geometry & MEMGEOM_DIMMSELECT) pbuf_len <<= 1;
		if (msys.mem_geometry & MEMGEOM_CHANSELECT) pbuf_len <<= 1;

		if (autopres) {
			void *presbuf = mmap(NULL, pbuf_len, PROT_READ | PROT_WRITE, press_mmap_flags, -1, 0);
			if (presbuf == MAP_FAILED) {
				perror("Pressure buffer allocation failed");
				return 2;
			}
			ctx.ap_tcount = setup_targets(&(ctx.ap_targets), presbuf, pbuf_len, &msys);
			ctx.hammer_opts |= HAM_OPT_AUTOPRESSURE;
#if 0 // HACK: Poor-man's debug switch
			for (size_t i = 0; i < ctx.ap_tcount; i++) {
				print_entry(ctx->outf, ctx.ap_targets[i], "\n");
			}
			return 42;
#endif
		}
		if (mem_pressure) {
			PRESSURE_BUF_LEN = pbuf_len;
			if (gran_override > 0) {
				PRESSURE_GRAN_SHIFT = gran_override;
			} else {
				for (size_t gran = ramses_map_granularity(msys.controller, msys.mem_geometry, msys.controller_opts); gran > 1; gran >>= 1)
					PRESSURE_GRAN_SHIFT++;
			}
			for (;mem_pressure --> 0;) {
				pthread_t pt;
				void *presbuf = mmap(NULL, PRESSURE_BUF_LEN, PROT_READ | PROT_WRITE, press_mmap_flags, -1, 0);
				if (presbuf == MAP_FAILED) {
					perror("Pressure buffer allocation failed");
					return 2;
				}
				if (pthread_create(&pt, NULL, pressure_func, presbuf)) {
					fprintf(stderr, "Error creating pressure thread\n");
					return 2;
				}
			}
		}
	}

	if (VERBOSITY & V_INFO) {
		fputs("Profiling...\n", stderr);
	}
	setlinebuf(ctx.outf);
	retval = run_profile(&ctx);
	if (VERBOSITY & V_INFO) {
		fputs("Finished\n", stderr);
	}

	return retval;
	err_usage:
		fprintf(stderr, "Usage: %s -s <buffer_size> [-hvdpwalHi] [-O<2sd>] [-W <window_radius>] "
						"[-D <target_distance>] [-I <refresh_intervals>] [-P <victim_pattern>]"
						"[-T <target_pattern>] [-g <pressure_gran>] MSYS_FILE [OUT_FILE]\n",
						basename(argv[0]));
		return 1;
}
