/*
 * Copyright (c) 2016 Andrei Tatar
 * Copyright (c) 2017-2018 Vrije Universiteit Amsterdam
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

#include "hamqueue.h"

#include <ramses/types.h>
#include <ramses/util.h>


bool row_fully_mapped(struct BufferMap *bm, struct BMPos p)
{
	return (ramses_bufmap_addr(bm, p.ri, p.ei).col == 0 &&
	        bm->ranges[p.ri].entry_cnt - p.ei >= ramses_bufmap_epr(bm));
}

static void hamqueue_push(struct HamQueue *q, struct BMPos v)
{
	if (q->count == q->len) {
		q->q[q->head] = v;
		q->head = hamqueue_idx(q, 1);
	} else {
		q->q[hamqueue_idx(q, q->count)] = v;
		q->count++;
	}
}

static inline struct BMPos last_q(struct HamQueue *q)
{
	return q->q[hamqueue_idx(q, q->count - 1)];
}

static inline bool succ_rows(struct DRAMAddr a, struct DRAMAddr b)
{
	return ramses_dramaddr_same(DRAM_BANK, a, b) && (a.row + 1 == b.row);
}

/* Sanity check that queue is primed */
static inline void sanity_q(struct HamQueue *q, struct BufferMap *bm, int s)
{
	if (!q->count) {
		hamqueue_push(q, (struct BMPos){0 ,0});
	}
	if (s && q->count == 1) {
		struct BMPos last = last_q(q);
		struct DRAMAddr laddr = ramses_bufmap_addr(bm, last.ri, last.ei);
		while (!row_fully_mapped(bm, last) &&
		       ramses_dramaddr_cmp(laddr, RAMSES_BADDRAMADDR))
		{
			last = ramses_bufmap_next(bm, last, DRAM_ROW);
			laddr = ramses_bufmap_addr(bm, last.ri, last.ei);
		}
		q->q[hamqueue_idx(q, 0)] = last;
	}
}


/* Fill queue with successive rows if possible */
static bool fill_queue(struct HamQueue *q, struct BufferMap *bm, int s)
{
	sanity_q(q, bm, s);
	struct BMPos last = last_q(q);
	while (q->count < q->len) {
		struct BMPos next = ramses_bufmap_next(bm, last, DRAM_ROW);
		if ((!s || row_fully_mapped(bm, next)) &&
		    succ_rows(ramses_bufmap_addr(bm, last.ri, last.ei),
		              ramses_bufmap_addr(bm, next.ri, next.ei)))
		{
			hamqueue_push(q, next);
			last = next;
		} else {
			return false;
		}
	}
	return true;
}

/* Push the next successive row into the queue if possible */
static bool step_queue(struct HamQueue *q, struct BufferMap *bm, int s)
{
	sanity_q(q, bm, s);
	struct BMPos last = last_q(q);
	struct BMPos next = ramses_bufmap_next(bm, last, DRAM_ROW);
	if ((!s || row_fully_mapped(bm, next)) &&
	    succ_rows(ramses_bufmap_addr(bm, last.ri, last.ei),
	              ramses_bufmap_addr(bm, next.ri, next.ei)))
	{
		hamqueue_push(q, next);
		return true;
	} else {
		return false;
	}
}

bool hamqueue_ready(struct HamQueue *q, struct BufferMap *bm, int s)
{
	sanity_q(q, bm, s);
	while (true) {
		if ((q->count == q->len) ?
		    step_queue(q, bm, s) :
		    fill_queue(q, bm, s))
		{
			return true;
		} else {
			struct BMPos next = ramses_bufmap_next(bm, last_q(q), DRAM_ROW);
			struct DRAMAddr na = ramses_bufmap_addr(bm, next.ri, next.ei);
			if (ramses_dramaddr_cmp(na, RAMSES_BADDRAMADDR) != 0) {
				hamqueue_clear(q);
				hamqueue_push(q, next);
			} else {
				return false;
			}
		}
	}
}
