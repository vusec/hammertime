/*
 * Copyright (c) 2017-2018 Vrije Universiteit Amsterdam
 *
 * This program is licensed under the GPL2+.
 */

#ifndef PROFILE_H
#define PROFILE_H 1

#include "hammer.h"

#include <ramses/bufmap.h>

#include <stddef.h>
#include <stdint.h>

/*
 * Callback for checking a particular pair of aggressor rows before hammering.
 * Returns 0 if attack should proceed, non-zero to skip
 */
typedef int (*attack_check_fn_t)(struct AddrEntry, struct AddrEntry, void *);
/* Callback for when a bit flip is found */
typedef void (*bitflip_report_fn_t)(struct AddrEntry, size_t, uint8_t, uint8_t, void *);
/* Callback for when an attack ends */
typedef void (*attack_end_fn_t)(void *);

struct ProfileCtx {
	struct BufferMap *bm; /* Main hammer buffer */

	const void *tpat; /* Pattern to fill target (aggressor) rows with */
	size_t tpatlen;
	const void *vpat; /* Pattern to fill victim rows with */
	size_t vpatlen;

	hammerfunc_t hamfunc; /* Hammer function used to trigger rowhammer */
	long hamopt; /* Hammer function option argument */
	attack_check_fn_t attack_check_fn; /* Attack check callback */
	void *attack_check_fn_arg;
	bitflip_report_fn_t bitflip_report_fn; /* Bitflip callback */
	void *bitflip_report_fn_arg;
	attack_end_fn_t attack_end_fn; /* Attack end callback */
	void *attack_end_fn_arg;

	void *extra; /* Extra argument, used by some profiling loops */

	unsigned long cal; /* Hammer iterations per refresh interval */
	unsigned int cal_mult; /* Refresh intervals to hammer for */
	unsigned int width; /* No. of rows checked on each side of aggressor row(s) */
	unsigned int dist; /* No. of rows between aggressors (double-sided only) */
	int invert_pat; /* If non-zero, run with bit-inverted patterns as well */
	int incomplete; /* If non-zero, enables hammering on rows not fully mapped */
};

/*
 * Do a single-sided rowhammer profile run
 * Requires ctx->extra to be set to an extra BufferMap
 */
void profile_singlesided(struct ProfileCtx *ctx);
/* Do a double-sided rowhammer profile run */
void profile_doublesided(struct ProfileCtx *ctx);

#endif /* profile.h */
