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

#include "glue.h"

#include "memfiles.h"

#include <ramses/resolve.h>
#include <ramses/vtlbucket.h>
#include <ramses/vtlb_hashtbl.h>

#include <stdio.h>

#define REVTLB_SIZE	0x4000
#define MAXREQS	128


static inline void pmem_flipreqs(struct MemorySystem *msys, int pmemfd,
                                 struct PredictorReq *preq, int reqs)
{
	for (int i = 0; i < reqs && i < MAXREQS; i++) {
		if (preq[i].type == REQ_BITFLIP) {
			physaddr_t tpa = ramses_resolve_reverse(msys, preq[i].addr);
			if (memfile_flip_bits(pmemfd,
			                      tpa + preq[i].arg.fliparg.cell_off,
			                      preq[i].arg.fliparg.pullup,
			                      preq[i].arg.fliparg.pulldown))
			{
				perror("ERROR FLIPPING BITS");
			}
		}
	}
}

void pmem_flip_loop(struct ProbeOutput *pout, struct Predictor *pred,
                    struct MemorySystem *msys, int pmemfd)
{
	uint64_t cur = 0, head = 0;
	uint64_t sz = pout->data_size;
	uint8_t *dp = pout->data;
	struct PredictorReq preq[MAXREQS];
	int64_t last_t = 0;
	int skip = ((pout->fmtflags & PROBEOUT_VIRTADDR) ? sizeof(uint64_t) : 0) +
	           ((pout->fmtflags & PROBEOUT_OPSTATS) ? sizeof(struct MemOpStats) : 0);

	while (!probeout_read_head(pout, cur, &head)) {
		while (cur < head) {
			uint64_t pa = *(uint64_t *)(dp + (cur % sz));
			cur += sizeof(pa);
			if (pa == RAMSES_BADADDR) {
				/* Update time */
				int64_t t = *(int64_t *)(dp + (cur % sz));
				cur += sizeof(t);
				if (t < 0) {
					t = -t;
				} else {
					int64_t tmp = t;
					t = t - last_t;
					last_t = tmp;
				}
				int reqs = pred->advance_time(pred->ctx, t, preq, MAXREQS);
				pmem_flipreqs(msys, pmemfd, preq, reqs);
			} else {
				cur += skip;
				/* Log data */
				struct DRAMAddr da = ramses_resolve(msys, (physaddr_t)pa);
				int reqs = pred->log_op(pred->ctx, da, preq, MAXREQS);
				pmem_flipreqs(msys, pmemfd, preq, reqs);
			}
		}
	}
}

static inline void vmem_flipreqs(struct MemorySystem *msys, pid_t pid,
                                 struct VTLBucketFuncs bf, void *htbl,
                                 struct PredictorReq *preq, int reqs)
{
	int64_t handle;
	for (int i = 0; i < reqs && i < MAXREQS; i++) {
		if (preq[i].type == REQ_BITFLIP) {
			physaddr_t tpa = ramses_resolve_reverse(msys, preq[i].addr);
			if (!ramses_vtlbucket_search(&bf, htbl, tpa >> 12, &handle)) {
				uintptr_t vaddr = ramses_vtlbucket_get(&bf, htbl, handle) << 12;
				vaddr += tpa & 0xfff; /* Last 12 bits of tpa */
				int vmemfd = memfile_pidmem(pid, MEMFILE_WRITABLE);
				if (memfile_flip_bits(vmemfd,
				                      vaddr + preq[i].arg.fliparg.cell_off,
				                      preq[i].arg.fliparg.pullup,
				                      preq[i].arg.fliparg.pulldown))
				{
					perror("ERROR FLIPPING BITS");
				} else {
					//~ puts("ARTIFICIAL BITFLIP");
				}
				close(vmemfd);
			} else {
				//~ puts("BITFLIP TARGET UNKNOWN");
			}
		}
	}
}

void vmem_flip_loop(struct ProbeOutput *pout, struct Predictor *pred,
                    struct MemorySystem *msys, pid_t pid)
{
	uint64_t cur = 0, head = 0;
	uint64_t sz = pout->data_size;
	uint8_t *dp = pout->data;
	struct PredictorReq preq[MAXREQS];
	struct VTLBucketFuncs bf;
	void *htbl;
	int64_t handle;
	int64_t last_t = 0;

	if (!(pout->fmtflags & PROBEOUT_VIRTADDR)) {
		return;
	}
	htbl = ramses_vtlb_hashtbl_create(REVTLB_SIZE, ramses_hash_twang6432, 256);
	if (htbl == NULL) {
		return;
	}
	ramses_vtlb_hashtbl_register_funcs(&bf);

	while (!probeout_read_head(pout, cur, &head)) {
		while (cur < head) {
			uint64_t pa, va;
			pa = *(uint64_t *)(dp + (cur % sz));
			cur += sizeof(pa);
			va = *(uint64_t *)(dp + (cur % sz));
			cur += sizeof(va);
			if (pa == RAMSES_BADADDR) {
				/* Update time */
				int64_t t = (int64_t)va;
				if (t < 0) {
					t = -t;
				} else {
					t = t - last_t;
					last_t = (int64_t)va;
				}
				int reqs = pred->advance_time(pred->ctx, t, preq, MAXREQS);
				vmem_flipreqs(msys, pid, bf, htbl, preq, reqs);
			} else {
				/* Update reverse-TLB */
				uint64_t k = pa >> 12;
				ramses_vtlbucket_search(&bf, htbl, k, &handle);
				ramses_vtlbucket_insert(&bf, htbl, k, va >> 12, handle);
				/* Log data */
				struct DRAMAddr da = ramses_resolve(msys, (physaddr_t)pa);
				int reqs = pred->log_op(pred->ctx, da, preq, MAXREQS);
				vmem_flipreqs(msys, pid, bf, htbl, preq, reqs);
			}
		}
	}

	ramses_vtlb_hashtbl_destroy(htbl);
}
