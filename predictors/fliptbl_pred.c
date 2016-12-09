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

#include "fliptbl_pred.h"

#include <ramses/types.h>
#include <ramses/vtlb.h>
#include <ramses/util.h>

#include <stdlib.h>
#ifdef FLIPTBL_PRED_DEBUG
#include <stdio.h>
#endif

static const unsigned long REFRESH_INTERVAL_US = 64000;
static const unsigned long REFRESH_TOLERANCE = 2000;

struct ftbl_context {
	struct FlipTable *ft;
	void *counts;
	unsigned long thresh;
	int dist;
	enum ExtrapMode extrap;

	#ifdef FLIPTBL_PRED_DEBUG
	struct DRAMAddr lastmax;
	uint64_t maxtally;
	uint64_t total;
	#endif
};

static void ftbl_destroy(void *ctx)
{
	struct ftbl_context *c = (struct ftbl_context *)ctx;
	ramses_vtlb_destroy(c->counts);
	free(ctx);
}

static int ftbl_advtime(void *ctx, int64_t timed, struct PredictorReq *reqs, int maxreq)
{
	struct ftbl_context *c = (struct ftbl_context *)ctx;
	ramses_vtlb_update_timedelta(c->counts, timed);
	#ifdef FLIPTBL_PRED_DEBUG
	fliptbl_pred_print_stats(ctx);
	c->maxtally = 0;
	#endif
	return 0;
}

static int ftbl_ansreq(void *ctx, uint32_t reqtag, void *arg,
                       struct PredictorReq *reqs, int maxreq)
{
	return 0;
}

static int ftbl_lookup(struct ftbl_context *c, struct DRAMAddr addr,
                       struct PredictorReq *reqs, int maxreq)
{
	struct Flip *flips;
	struct DRAMAddr ediff = {0, 0, 0, 0, 0, 0};
	int nflips = fliptbl_lookup(c->ft, addr, c->extrap, &flips, &ediff);
	#ifdef FLIPTBL_PRED_DEBUG
	printf("Lookup: ");
	printf(DRAMADDR_HEX_FMTSTR, addr.chan, addr.dimm, addr.rank, addr.bank, addr.row, addr.col);
	printf(" %d\n", nflips);
	#endif
	for (int i = 0; i < nflips && i < maxreq; i++) {
		reqs[i].type = REQ_BITFLIP;
		reqs[i].tag = 0;
		reqs[i].addr = ramses_dramaddr_add(flips[i].location, ediff);
		reqs[i].arg.fliparg.cell_off = flips[i].cell_byte;
		reqs[i].arg.fliparg.pullup = flips[i].pullup;
		reqs[i].arg.fliparg.pulldown = flips[i].pulldown;
	}
	return nflips;
}

static int ftbl_logop(void *ctx, struct DRAMAddr addr,
                      struct PredictorReq *reqs, int maxreq)
{
	struct ftbl_context *c = (struct ftbl_context *)ctx;
	#ifdef FLIPTBL_PRED_DEBUG
	c->total++;
	#endif

	addr.col = 0;
	uint64_t key = *((uint64_t *)&addr);
	physaddr_t tally = ramses_vtlb_search(c->counts, key);
	if (tally == RAMSES_BADADDR) {
		ramses_vtlb_update(c->counts, key, 1);
		return 0;
	} else {
		tally++;
	}
	ramses_vtlb_update(c->counts, key, tally);
	#ifdef FLIPTBL_PRED_DEBUG
	if (tally > c->maxtally) {
		c->maxtally = tally;
		#ifdef FLIPTBL_PRED_VERBDEBUG
		if (ramses_dramaddr_cmp(addr, c->lastmax) != 0) {
			printf(DRAMADDR_HEX_FMTSTR, addr.chan, addr.dimm, addr.rank, addr.bank, addr.row, addr.col);
			puts("");
			c->lastmax = addr;
		}
		#endif
	}
	#endif

	if (tally >= c->thresh) {
		struct DRAMAddr o;
		uint64_t okey;
		physaddr_t otally;
		/* See if lower row is also targeted */
		o = ramses_dramaddr_addrows(addr, -(c->dist));
		okey = *((uint64_t *)&o);
		otally = ramses_vtlb_search(c->counts, okey);
		if (otally != RAMSES_BADADDR) {
			if (otally >= c->thresh) {
				ramses_vtlb_update(c->counts, okey, 0);
				ramses_vtlb_update(c->counts, key, 0);
				return ftbl_lookup(c, o, reqs, maxreq);
			} else {
				#ifdef FLIPTBL_PRED_VERBDEBUG
				printf(">%ld<\n", otally);
				#endif
			}
		}
		/* See if upper row is also targeted */
		o = ramses_dramaddr_addrows(addr, c->dist);
		okey = *((uint64_t *)&o);
		otally = ramses_vtlb_search(c->counts, okey);
		if (otally != RAMSES_BADADDR) {
			if (otally >= c->thresh) {
				ramses_vtlb_update(c->counts, okey, 0);
				ramses_vtlb_update(c->counts, key, 0);
				return ftbl_lookup(c, addr, reqs, maxreq);
			} else {
				#ifdef FLIPTBL_PRED_VERBDEBUG
				printf(">%ld<\n", otally);
				#endif
			}
		}
	}

	return 0;
}

int init_fliptbl_predictor(struct Predictor *p, struct FlipTable *ft,
                           enum HammerMode mode, unsigned long flip_thresh,
                           enum ExtrapMode extrap)
{
	void *cnt;
	struct ftbl_context *c = malloc(sizeof(*c));
	if (c == NULL) {
		return FLIPTBL_PRED_ERR_MEMALLOC;
	}
	switch (mode) {
		case HAMMER_SINGLESIDED:
			c->dist = 0;
			break;
		case HAMMER_DOUBLESIDED:
			c->dist = 2;
			break;
		default:
			return FLIPTBL_PRED_ERR_INVMODE;
	}
	if (c->dist != ft->dist) {
		return FLIPTBL_PRED_ERR_FLIPTBL;
	}
	cnt = ramses_vtlb_create(512000, 1, REFRESH_INTERVAL_US,
	                         REFRESH_INTERVAL_US + REFRESH_TOLERANCE, -1);
	if (cnt == NULL) {
		return FLIPTBL_PRED_ERR_COUNTERS;
	}

	c->extrap = extrap;
	c->thresh = flip_thresh;
	c->ft = ft;
	c->counts = cnt;
	#ifdef FLIPTBL_PRED_DEBUG
	c->maxtally = 0;
	c->total = 0;
	#endif

	p->ctx = c;
	p->destroy = ftbl_destroy;
	p->advance_time = ftbl_advtime;
	p->answer_req = ftbl_ansreq;
	p->log_op = ftbl_logop;
	return 0;
}

#ifdef FLIPTBL_PRED_DEBUG
void fliptbl_pred_print_stats(void *ctx)
{
	struct ftbl_context *c = (struct ftbl_context *)ctx;
	printf("%ld %ld\n", c->maxtally, c->total);
}
#endif
