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

#include "dram_load_mp.h"

#include "common.h"
#include "perfev-util/perfev.h"
#include "perfev-util/pollster.h"

#include <ramses/types.h>
#include <ramses/translate.h>
#include <ramses/vtlb.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <unistd.h>

static const char *EVENT_STR = "MEM_LOAD_UOPS_LLC_MISS_RETIRED:LOCAL_DRAM";
static const uint64_t SAMPLE_PERIOD = 2;
static const uint64_t WAKEUP_EVENTS = 8192;
static const int MMAP_PAGES = 512;

static const unsigned int VTLB_SIZE = 128000;
static const unsigned int VTLB_NGEN = 3;
static const unsigned long VTLB_MIN_TRUST = 4000000;
static const unsigned long VTLB_MAX_TRUST = 10000000;

static const unsigned int PMC_SIZE = 512;

struct pmc_entry {
	pid_t pid;
	int fd;
};

struct probe_state {
	struct perfevprobe_state st;
	void *vtlb;
	pid_t self;
	struct pmc_entry *pmc;

	#ifdef PEPROBE_MP_DEBUG
	uint64_t go;
	uint64_t fo;
	uint64_t co;
	uint64_t fa;
	#endif
};

/* Determined by peattr.sample_type */
struct perf_record_sample {
	struct perf_event_header header;
	uint32_t pid, tid;
	uint64_t addr;
};

static int decode(void *state, void *record,
                  uint64_t *pa, uint64_t *va, struct MemOpStats *ostat)
{
	struct probe_state *s = (struct probe_state *)state;
	struct perf_record_sample *pr = (struct perf_record_sample *)record;
	/* Ignore NULLs */
	if (!pr->addr) {
		return 1;
	}
	/* Don't monitor self */
	if (pr->pid == s->self) {
		return 1;
	}
	uintptr_t vpfn = pr->addr >> 12;
	uintptr_t voff = pr->addr & 0xfff;
	uint64_t pid = pr->pid;
	/* HACK: This relies on virtual addresses fitting within 48 bits and PIDs
	 * within 28 bits (currently a sane assumption; the future may change that)
	 */
	uint64_t k = vpfn + (pid << 36);

	physaddr_t vpa = ramses_vtlb_search(s->vtlb, k);
	if (vpa == RAMSES_BADADDR) {
		int pm;
		int pmc_loc = pid % PMC_SIZE;
		if (s->pmc[pmc_loc].pid == pid) {
		//~ if (0) {
			pm = s->pmc[pmc_loc].fd;
			#ifdef PEPROBE_MP_DEBUG
			s->co++;
			#endif
		} else {
			pm = get_pagemap_fd(pid);
			if (pm >= 0) {
				if (s->pmc[pmc_loc].pid) {
					close(s->pmc[pmc_loc].fd);
				}
				s->pmc[pmc_loc].pid = pid;
				s->pmc[pmc_loc].fd = pm;
				#ifdef PEPROBE_MP_DEBUG
				s->go++;
				#endif
			} else {
				#ifdef PEPROBE_MP_DEBUG
				s->fo++;
				#endif
			}
		}
		if (pm >= 0) {
			vpa = ramses_translate_pagemap(vpfn << 12, pm);
			if (vpa != RAMSES_BADADDR) {
				vpa >>= 12;
				ramses_vtlb_update(s->vtlb, k, vpa);
			} else if (errno != ENODATA) {
				close(pm);
				s->pmc[pmc_loc].pid = 0;
			}
		} else {
			#ifdef PEPROBE_MP_DEBUG
			s->fa++;
			#endif
			return 1;
		}
	}
	if (vpa != RAMSES_BADADDR) {
		*pa = (vpa << 12) + voff;
		if (va) {
			*va = (uint64_t)pr->addr;
		}
		if (ostat) {
			struct MemOpStats opst = {
				.pid = pid,
				.isstore = 0,
				.reserved = 0,
				.custflags = 0,
			};
			*ostat = opst;
		}
		return 0;
	} else {
		return 1;
	}
}

static void data_cb(int fd, int64_t time, struct PerfMMAP *mmap, void *arg)
{
	struct probe_state *s = (struct probe_state *)arg;
	ramses_vtlb_update_timestamp(s->vtlb, time);
	perfevprobe_sample_cb(time, mmap, s->st.out, 1, decode, s, NULL, NULL);

	#ifdef RAMSES_VTLB_DEBUG
	printf("FD: %d; VTLB: %6.2f%% hit, %6.2f probeavg, %10.1fns hit avg, %10.1fns miss avg, %9ld reqs\n",
	       fd,
	       ramses_vtlb_hitrate(s->vtlb) * 100.0, ramses_vtlb_avg_probe(s->vtlb),
	       ramses_vtlb_avg_hit_time(s->vtlb), ramses_vtlb_avg_miss_time(s->vtlb),
	       ramses_vtlb_get_nreqs(s->vtlb));
	ramses_vtlb_clear_stats(s->vtlb);
	#endif
}

static int destroy_f(void *c)
{
	struct probe_state *s = (struct probe_state *)c;
	int cnt = 0;
	for (int i = 0; i < PMC_SIZE; i++) {
		if (s->pmc[i].pid) {
			close(s->pmc[i].fd);
			cnt++;
		}
	}
	#ifdef PEPROBE_MP_DEBUG
	printf("FA: %ld; GO: %ld; FO: %ld; CO: %ld; F: %d\n", s->fa, s->go, s->fo, s->co, cnt);
	#endif
	free(s->pmc);
	ramses_vtlb_destroy(s->vtlb);
	return perfevprobe_destroy_f(c);
}

static inline int setup_perfev(struct perf_event_attr *peattr)
{
	int ret;
	ret = perfev_encode(EVENT_STR, peattr);
	if (!ret) {
		struct perf_event_attr *p = peattr;
		p->size = sizeof(struct perf_event_attr);
		p->exclude_kernel = 1;
		p->exclude_user = 0;
		p->pinned = 0;
		p->read_format = 0;
		p->inherit = 0;
		p->sample_period = SAMPLE_PERIOD;
		p->wakeup_events = WAKEUP_EVENTS;
		p->precise_ip = 2;
		p->disabled = 1;
		p->sample_type = PERF_SAMPLE_ADDR | PERF_SAMPLE_TID;
		p->comm = 0;
		p->comm_exec = 0;
	}
	return ret;
}

int probe_dramload_setup_sys(struct ProbeOutput *pout, struct ProbeControlPanel *pcp)
{
	int ret;
	int numcpu = sysconf(_SC_NPROCESSORS_ONLN);

	struct probe_state *st;
	struct perf_event_attr peattr;
	struct perf_event_attr *ppeattr = &peattr;
	struct PerfevResult r[numcpu];
	struct PollsterCtx *pc;
	struct PollsterFd *fds;
	void *vtlbuf = NULL;
	struct pmc_entry *pmc;

	if (probeout_check_size(pout)) {
		errno = EINVAL;
		return DRAMLOAD_ERR_POUT;
	}
	memset(&peattr, 0, sizeof(peattr));
	if (setup_perfev(&peattr)) {
		return DRAMLOAD_ERR_PERFEV;
	}
	if (perfev_attach_pid(&ppeattr, 1, PERFEV_FLAG_STRICT | PERFEV_FLAG_PERCPU, -1, -1, r) != numcpu) {
		return DRAMLOAD_ERR_ATTACH;
	}

	st = malloc(sizeof(*st));
	pc = malloc(sizeof(*pc));
	fds = malloc(numcpu * sizeof(*fds));
	pmc = malloc(PMC_SIZE * sizeof(*pmc));
	if (!st || !pc || !fds || !pmc) {
		ret = DRAMLOAD_ERR_MEMALLOC;
		goto err_cleanup;
	}
	memset(st, 0, sizeof(*st));
	memset(pc, 0, sizeof(*pc));
	memset(fds, 0, numcpu * sizeof(*fds));
	memset(pmc, 0, PMC_SIZE * sizeof(struct pmc_entry));

	vtlbuf = ramses_vtlb_create(VTLB_SIZE, VTLB_NGEN, VTLB_MIN_TRUST,
	                            VTLB_MAX_TRUST, -1);
	if (vtlbuf == NULL) {
		ret = DRAMLOAD_ERR_VTLB;
		goto err_cleanup;
	}

	pout->head = 0;
	pout->finished = 0;
	pout->sample_loss = 0; /* TODO: actually calibrate */

	st->st.pctx = pc;
	st->st.out = pout;
	st->st.status = 0;
	st->vtlb = vtlbuf;
	st->self = getpid();
	st->pmc = pmc;

	pc->num_fds = numcpu;
	pc->flags = 0;
	pc->fds = fds;
	pc->misc_callback = NULL;
	pc->misc_arg = NULL;
	pc->end_callback = perfevprobe_end_cb;
	pc->end_arg = st->st.out;

	for (int i = 0; i < numcpu; i++) {
		fds[i].mmap_pages = MMAP_PAGES;
		fds[i].callback = data_cb;
		fds[i].arg = st;
		fds[i].fl_timestamp = 1;
		fds[i].fd = r[i].fd;
	}

	perfevprobe_setup_cpfuncs(pcp);
	pcp->func[PROBE_DESTROY] = destroy_f;
	pcp->ctx = st;

	if (pthread_create(&st->st.pollster_thread, NULL,
	                   pollster_run_f, st->st.pctx)) {
		destroy_f(st);
		return DRAMLOAD_ERR_POLLSTER;
	}
	st->st.status |= PROBE_STATUS_STARTED | PROBE_STATUS_TARGET_STARTED |
	                 PROBE_STATUS_TARGET_RUNNING;
	return 0;

	err_cleanup:
	if (vtlbuf) ramses_vtlb_destroy(vtlbuf);
	if (pmc) free(pmc);
	if (fds) free(fds);
	if (pc) free(pc);
	if (st) free(st);
	return ret;
}
