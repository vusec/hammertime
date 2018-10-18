/*
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

#include "profile.h"

#include "hamqueue.h"
#include "mem.h"

#include <ramses/bufmap.h>

#include <alloca.h>
#include <assert.h>

static inline size_t get_row_entries(struct BufferMap *bm, struct BMPos p,
                                     struct AddrEntry *ents, size_t elen)
{
	struct BMPos end = ramses_bufmap_next(bm, p, DRAM_ROW);
	return ramses_bufmap_get_entries(bm, p, end, ents, elen);
}

static void fill_row(struct BufferMap *bm, struct HamQueue *q, hamqueue_off_t qi,
                     const void *pat, size_t patlen, int inverted)
{
	const size_t ENTSLEN = ramses_bufmap_epr(bm);
	struct AddrEntry ents[ENTSLEN];
	size_t ecnt = get_row_entries(bm, q->q[hamqueue_idx(q, qi)], ents, ENTSLEN);
	for (size_t i = 0; i < ecnt; i++) {
		mem_fill((void *)ents[i].virtp, bm->entry_len, pat, patlen, inverted);
		mem_flush((void *)ents[i].virtp, bm->entry_len);
	}
}

static void check_row(struct ProfileCtx *c, struct HamQueue *q, hamqueue_off_t qi,
                      const void *pat, size_t patlen, int inverted)
{
	const size_t ENTSLEN = ramses_bufmap_epr(c->bm);
	struct AddrEntry ents[ENTSLEN];
	size_t ecnt = get_row_entries(c->bm, q->q[hamqueue_idx(q, qi)], ents, ENTSLEN);
	for (size_t i = 0; i < ecnt; i++) {
		size_t ep = 0;
		while ((ep = mem_check((void *)ents[i].virtp, c->bm->entry_len, pat,
		                       patlen, ep, inverted)) < c->bm->entry_len)
		{
			uint8_t exp = ((uint8_t *)pat)[ep % patlen];
			uint8_t got = ((uint8_t *)ents[i].virtp)[ep];
			assert(exp != got);
			if (inverted) exp = ~exp;
			if (c->bitflip_report_fn) {
				c->bitflip_report_fn(ents[i], ep, exp, got,
				                     c->bitflip_report_fn_arg);
			}
			ep++;
		}
	}
}

static inline void fill_rows(struct BufferMap *bm, struct HamQueue *q,
                             hamqueue_off_t l, hamqueue_off_t r,
                             const void *pat, size_t patlen, int inv)
{
	for (hamqueue_off_t i = l; i < r; i++) {
		fill_row(bm, q, i, pat, patlen, inv);
	}
}

static inline void check_rows(struct ProfileCtx *p, struct HamQueue *q,
                              hamqueue_off_t l, hamqueue_off_t r,
                              const void *pat, size_t patlen, int inv)
{
	for (hamqueue_off_t i = l; i < r; i++) {
		check_row(p, q, i, pat, patlen, inv);
	}
}


void profile_singlesided(struct ProfileCtx *c)
{
	size_t qlen = 1 + 2*c->width;
	struct HamQueue *q = alloca(hamqueue_size(qlen));
	q->len = qlen;
	hamqueue_clear(q);
	struct AddrEntry ae[2];
	struct BufferMap *xbm = (struct BufferMap *)c->extra;
	int r;
	if (xbm == NULL) {
		return;
	}

	const hamqueue_off_t mid = q->len / 2;

	while (hamqueue_ready(q, c->bm, !c->incomplete)) {
		r = ramses_bufmap_get_entry(c->bm, q->q[hamqueue_idx(q, mid)], &ae[0]);
		assert(!r);
		struct BMPos pair = { 0, 0 };
		(void) ramses_bufmap_find_same(xbm, ae[0].dramaddr, DRAM_BANK, &pair);
		r = ramses_bufmap_get_entry(xbm, pair, &ae[1]);
		assert(!r);
		if (c->attack_check_fn &&
		    c->attack_check_fn(ae[0], ae[1], c->attack_check_fn_arg))
		{
			continue;
		}
		/* All clear */
		for (int inv = 0; inv <= (c->invert_pat ? 0 : 1); inv++) {
			fill_rows(c->bm, q, 0, mid, c->vpat, c->vpatlen, inv);
			fill_row(c->bm, q, mid, c->tpat, c->tpatlen, inv);
			fill_rows(c->bm, q, mid+1, q->len, c->vpat, c->vpatlen, inv);

			c->hamfunc(ae[0].virtp, ae[1].virtp, c->cal * c->cal_mult, c->hamopt);

			check_rows(c, q, 0, mid, c->vpat, c->vpatlen, inv);
			check_rows(c, q, mid+1, q->len, c->vpat, c->vpatlen, inv);
			/* Sanity check aggressor row */
			check_row(c, q, mid, c->tpat, c->tpatlen, inv);
		}
		if (c->attack_end_fn) {
			c->attack_end_fn(c->attack_end_fn_arg);
		}
	}
}

void profile_doublesided(struct ProfileCtx *c)
{
	size_t qlen = 2 + 2*c->width + c->dist;
	struct HamQueue *q = alloca(hamqueue_size(qlen));
	q->len = qlen;
	hamqueue_clear(q);
	struct AddrEntry ae[2];
	int r;

	const hamqueue_off_t mid = q->len / 2;
	const hamqueue_off_t t1 = mid - 1 + c->dist / 2;
	const hamqueue_off_t t2 = mid + c->dist / 2 + c->dist % 2;

	while (hamqueue_ready(q, c->bm, !c->incomplete)) {
		r = ramses_bufmap_get_entry(c->bm, q->q[hamqueue_idx(q, t1)], &ae[0]);
		assert(!r);
		r = ramses_bufmap_get_entry(c->bm, q->q[hamqueue_idx(q, t2)], &ae[1]);
		assert(!r);

		if (c->attack_check_fn &&
			c->attack_check_fn(ae[0], ae[1], c->attack_check_fn_arg))
		{
			continue;
		}
		/* All clear */
		for (int inv = 0; inv <= (c->invert_pat ? 1 : 0); inv++) {
			fill_rows(c->bm, q, 0, t1, c->vpat, c->vpatlen, inv);
			fill_row(c->bm, q, t1, c->tpat, c->tpatlen, inv);
			fill_rows(c->bm, q, t1+1, t2, c->vpat, c->vpatlen, inv);
			fill_row(c->bm, q, t2, c->tpat, c->tpatlen, inv);
			fill_rows(c->bm, q, t2+1, q->len, c->vpat, c->vpatlen, inv);

			c->hamfunc(ae[0].virtp, ae[1].virtp, c->cal * c->cal_mult, c->hamopt);

			check_rows(c, q, 0, t1, c->vpat, c->vpatlen, inv);
			check_rows(c, q, t1+1, t2, c->vpat, c->vpatlen, inv);
			check_rows(c, q, t2+1, q->len, c->vpat, c->vpatlen, inv);
			/* Sanity check aggressor rows */
			check_row(c, q, t1, c->tpat, c->tpatlen, inv);
			check_row(c, q, t2, c->tpat, c->tpatlen, inv);
		}
		if (c->attack_end_fn) {
			c->attack_end_fn(c->attack_end_fn_arg);
		}
	}
}
