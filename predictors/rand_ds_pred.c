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

#include "rand_ds_pred.h"

#include <ramses/util.h>

#include <stdlib.h>
#include <string.h>

struct rds_context {
	int chance;
	struct DRAMAddr last;
};

static void rds_destroy(void *ctx)
{
	free(ctx);
}
static int rds_advtime(void *ctx, int64_t timed, struct PredictorReq *reqs, int maxreq)
{
	return 0;
}
static int rds_ansreq(void *ctx, uint32_t reqtag, void *arg,
                      struct PredictorReq *reqs, int maxreq)
{
	return 0;
}
static int rds_logop(void *ctx, struct DRAMAddr a,
                      struct PredictorReq *reqs, int maxreq)
{
	struct rds_context *c = (struct rds_context *)ctx;
	if (ramses_same_bank(c->last, a) && c->last.row + 2 == a.row) {
		if (random() < c->chance) {
			if (maxreq > 0) {
				reqs[0].type = REQ_BITFLIP;
				reqs[0].tag = 0;
				reqs[0].addr = c->last;
				reqs[0].addr.row++;
				reqs[0].arg.fliparg.cell_off = 0;
				reqs[0].arg.fliparg.pulldown = 0x5a;
				reqs[0].arg.fliparg.pullup = ~0x5a;
				return 1;
			}
		}
	}
	c->last = a;
	return 0;
}

int init_rand_ds_predictor(struct Predictor *p, int chance)
{
	struct rds_context *c = malloc(sizeof(*c));
	if (c == NULL) {
		return 1;
	}
	c->chance = chance;
	memset(&c->last, 0, sizeof(struct DRAMAddr));
	p->ctx = c;
	p->destroy = rds_destroy;
	p->advance_time = rds_advtime;
	p->answer_req = rds_ansreq;
	p->log_op = rds_logop;
	return 0;
}
