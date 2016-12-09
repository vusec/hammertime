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

/*
 * A predictor outputting bitflips based on fliptables.
 * It can naively identify a specific rowhammer access pattern, defined by
 * HammerMode, by counting the number of cache-missing memory accesses within
 * a DRAM refresh interval. The threshold depends on the runtime environment and
 * should be calibrated with a rowhammering tool (e.g. tools/profile)
 */

#ifndef _HAMTIME_FLIPTBL_PRED_H
#define _HAMTIME_FLIPTBL_PRED_H 1

#include "predictor.h"
#include "fliptable.h"

//~ #define FLIPTBL_PRED_DEBUG
//~ #define FLIPTBL_PRED_VERBDEBUG

enum HammerMode {
	HAMMER_SINGLESIDED,
	HAMMER_DOUBLESIDED
};

#define FLIPTBL_PRED_ERR_MEMALLOC	1 /* Error allocating memory for internal data structures */
#define FLIPTBL_PRED_ERR_INVMODE	2 /* Invalid HammerMode specified */
#define FLIPTBL_PRED_ERR_FLIPTBL	3 /* Fliptable incompatible with HammerMode */
#define FLIPTBL_PRED_ERR_COUNTERS	4 /* Error setting up per-row access counters */

int init_fliptbl_predictor(struct Predictor *p, struct FlipTable *ft,
                           enum HammerMode mode, unsigned long flip_thresh,
                           enum ExtrapMode extrap);

#ifdef FLIPTBL_PRED_DEBUG
void fliptbl_pred_print_stats(void *ctx);
#endif

#endif /* fliptbl_pred.h */
