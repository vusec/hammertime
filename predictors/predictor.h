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
 * Interface for hammertime predictors
 *
 * A predictor is essentially a black box that takes as input memory operations
 * (loads and stores) & advances in time, and responds with requests for
 * bitflips, data contents, etc.
 */

#ifndef _HAMTIME_PREDICTOR_H
#define _HAMTIME_PREDICTOR_H 1

#include <ramses/types.h>

#include <unistd.h>

enum ReqType {
	/*
	 * A bitflip is predicted to have occured
	 * arg is struct BitFlipArg
	 */
	REQ_BITFLIP,
	/*
	 * Request for memory contents at a particular address
	 * arg is the number of memory cells data is requested for
	 */
	REQ_DATA,
};

struct BitFlipArg {
	uint16_t cell_off;
	uint8_t pullup;
	uint8_t pulldown;
};

union ReqArg {
	uint64_t val;
	struct BitFlipArg fliparg;
};

struct PredictorReq {
	enum ReqType type; /* Type of request */
	uint32_t tag; /* Predictor-specific tag; used to uniquely identify a request */
	struct DRAMAddr addr; /* DRAM Address the request applies to */
	union ReqArg arg; /* Request-specific argument */
};

struct Predictor {
	void *ctx; /* Opaque pointer to the predictor's context */
	void (*destroy)(void *ctx); /* Free all resources associated with predictor */
	/* Advance the internal time state of the predictor by timed nanoseconds */
	int (*advance_time)(void *ctx, int64_t timed,
	                    struct PredictorReq *reqs, int maxreq);
	/* Log a memory operation that occured at addr */
	int (*log_op)(void *ctx, struct DRAMAddr addr,
	              struct PredictorReq *reqs, int maxreq);
	/* Respond to a previous request identified by reqtag, with the answer in arg */
	int (*answer_req)(void *ctx, uint32_t reqtag, void *arg,
	                  struct PredictorReq *reqs, int maxreq);
};

/*
 * Predictor functions (aside from destroy) can trigger requests to be generated.
 * These requests are written to *reqs, a buffer provided by the caller,
 * up to a maximum of maxreq entries.
 * The return value is the total number of requests generated, regardless of
 * maxreq and how many could be fit into *reqs.
 *
 * Thus, a return value greater than maxreq signifies there are additional
 * pending requests left. A predictor is free to decide if it should cache these
 * to return in a later call or simply drop them.
 * If it chooses the former, calling advance_time with a timed of 0 is the
 * correct no-op to grab pending requests.
 */

#endif /* predictor.h */
