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

#include "dram_load.h"

#include "common.h"

#include "perfev-util/perfev.h"
#include "perfev-util/perfev_child.h"
#include "perfev-util/pollster.h"

#include <ramses/types.h>
//~ #include <ramses/translate.h>
#include <ramses/vtlb.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/ioctl.h>


static const char *EVENT_STR = "MEM_LOAD_UOPS_LLC_MISS_RETIRED:LOCAL_DRAM";
static const uint64_t SAMPLE_PERIOD = 2;
static const uint64_t WAKEUP_EVENTS = 8192;
static const int MMAP_PAGES = 128;

static const unsigned int VTLB_SIZE = 48000;
static const unsigned int VTLB_NGEN = 3;
static const unsigned long VTLB_MIN_TRUST = 1000000;
static const unsigned long VTLB_MAX_TRUST = 10000000;

struct probe_state {
	struct perfevprobe_pid_state pst;
	int pmap_fd;
	void *vtlb;
};

/* Determined by peattr.sample_type */
struct perf_record_sample {
    struct perf_event_header header;
    uint64_t addr;
};

struct perf_record_comm {
	struct perf_event_header header;
	uint32_t pid;
	uint32_t tid;
	char comm[];
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
	//~ *pa = ramses_translate_pagemap(pr->addr, s->pmap_fd);
	uint64_t vpfn = pr->addr >> 12;
	uintptr_t voff = pr->addr & 0xfff;
	physaddr_t vpa = ramses_vtlb_lookup(s->vtlb, vpfn);
	if (vpa == RAMSES_BADADDR) {
		//~ perror("Translate error");
		//~ printf("%16lx\n", pr->addr);
		return 1;
	} else {
		*pa = (vpa << 12) + voff;
	}
	if (va) {
		*va = (uint64_t)pr->addr;
	}
	if (ostat) {
		struct MemOpStats opst = {
			.pid = s->pst.tpid,
			.isstore = 0,
			.reserved = 0,
			.custflags = 0,
		};
		*ostat = opst;
	}
	return 0;
}

static void rechandler(void *state, void *record)
{
	struct probe_state *s = (struct probe_state *)state;
	struct perf_event_header *ph = (struct perf_event_header *)record;
	if (ph->type == PERF_RECORD_COMM && ph->misc & PERF_RECORD_MISC_COMM_EXEC) {
		ramses_vtlb_flush(s->vtlb);
		ramses_vtlb_update_pagemapfd(s->vtlb, get_pagemap_fd(s->pst.tpid));
	}
}

static void data_cb_td(int fd, int64_t time, struct PerfMMAP *mmap, void *arg)
{
	//~ printf("td:%d\n", fd);
	struct probe_state *s = (struct probe_state *)arg;
	ramses_vtlb_update_timedelta(s->vtlb, time);
	perfevprobe_sample_cb(time, mmap, s->pst.st.out, 0, decode, s, rechandler, s);

	#ifdef RAMSES_VTLB_DEBUG
	printf("VTLB: %6.2f%% hit, %6.2f probeavg, %10.1fns hit avg, %10.1fns miss avg, %9ld reqs\n",
	       ramses_vtlb_hitrate(s->vtlb) * 100.0, ramses_vtlb_avg_probe(s->vtlb),
	       ramses_vtlb_avg_hit_time(s->vtlb), ramses_vtlb_avg_miss_time(s->vtlb),
	       ramses_vtlb_get_nreqs(s->vtlb));
	ramses_vtlb_clear_stats(s->vtlb);
	#endif
}
static void data_cb_ts(int fd, int64_t time, struct PerfMMAP *mmap, void *arg)
{
	//~ printf("ts:%d\n", fd);
	struct probe_state *s = (struct probe_state *)arg;
	ramses_vtlb_update_timestamp(s->vtlb, time);
	perfevprobe_sample_cb(time, mmap, s->pst.st.out, 1, decode, s, rechandler, s);

	#ifdef RAMSES_VTLB_DEBUG
	printf("VTLB: %6.2f%% hit, %6.2f probeavg, %10.1fns hit avg, %10.1fns miss avg, %9ld reqs\n",
	       ramses_vtlb_hitrate(s->vtlb) * 100.0, ramses_vtlb_avg_probe(s->vtlb),
	       ramses_vtlb_avg_hit_time(s->vtlb), ramses_vtlb_avg_miss_time(s->vtlb),
	       ramses_vtlb_get_nreqs(s->vtlb));
	ramses_vtlb_clear_stats(s->vtlb);
	#endif
}

static int misc_cb(int fd, short revents, void *arg)
{
	//~ printf("M: %d; fd: %d\n", revents, fd);
	return 0;
}

static int destroy_f(void *c)
{
	struct probe_state *s = (struct probe_state *)c;
	ramses_vtlb_destroy(s->vtlb);
	close(s->pmap_fd);
	return perfevprobe_pid_destroy_f(c);
}

static inline int setup_perfev(struct perf_event_attr *peattr, int multitask)
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
		p->inherit = (multitask) ? 1 : 0;
		p->sample_period = SAMPLE_PERIOD;
		p->wakeup_events = WAKEUP_EVENTS;
		p->precise_ip = 2;
		p->disabled = 1;
		p->sample_type = PERF_SAMPLE_ADDR;
		p->comm = 1;
		p->comm_exec = 1;
	}
	return ret;
}


static inline int setup_state(struct probe_state **state, pid_t target_pid,
                              struct PerfevResult *rs, int nfds,
                              struct ProbeOutput *pout, struct ProbeControlPanel *pcp)
{
	int ret;
	int pagemap_fd;

	struct PollsterCtx *pc;
	struct PollsterFd *fds;
	void *vtlbuf = NULL;

	*state = malloc(sizeof(**state));
	pc = malloc(sizeof(*pc));
	fds = malloc(nfds * sizeof(*fds));
	if (!*state || !pc || !fds) {
		ret = DRAMLOAD_ERR_MEMALLOC;
		goto err_cleanup;
	}
	struct probe_state *st = *state;
	memset(st, 0, sizeof(*st));
	memset(pc, 0, sizeof(*pc));
	memset(fds, 0, nfds * sizeof(*fds));

	pagemap_fd = get_pagemap_fd(target_pid);
	if (pagemap_fd < 0) {
		ret = DRAMLOAD_ERR_PAGEMAP;
		goto err_cleanup;
	}

	/* TODO: Tweak VTLB knobs */
	vtlbuf = ramses_vtlb_create(VTLB_SIZE, VTLB_NGEN, VTLB_MIN_TRUST,
	                            VTLB_MAX_TRUST, pagemap_fd);
	if (vtlbuf == NULL) {
		ret = DRAMLOAD_ERR_VTLB;
		goto err_cleanup;
	}

	pout->head = 0;
	pout->finished = 0;
	pout->sample_loss = 0; /* TODO: actually calibrate */

	st->pst.st.pctx = pc;
	st->pst.st.out = pout;
	st->pst.st.status = 0;
	st->pst.tpid = target_pid;
	st->pmap_fd = pagemap_fd;
	st->vtlb = vtlbuf;

	pc->num_fds = nfds;
	pc->flags = 0;
	pc->fds = fds;
	pc->misc_callback = misc_cb;
	pc->misc_arg = NULL;
	pc->end_callback = perfevprobe_end_cb;
	pc->end_arg = st->pst.st.out;

	for (int i = 0; i < nfds; i++) {
		fds[i].mmap_pages = MMAP_PAGES;
		fds[i].callback = (nfds == 1) ? data_cb_td : data_cb_ts;
		fds[i].arg = st;
		fds[i].fl_timestamp = (nfds == 1) ? 0 : 1;
		fds[i].fd = rs[i].fd;
	}

	perfevprobe_pid_setup_cpfuncs(pcp);
	pcp->func[PROBE_DESTROY] = destroy_f;
	pcp->ctx = st;
	return 0;

	err_cleanup:
	if (vtlbuf) ramses_vtlb_destroy(vtlbuf);
	if (fds) free(fds);
	if (pc) free(pc);
	if (*state) free(*state);
	return ret;
}


int probe_dramload_setup_child(struct ProbeOutput *pout, struct ProbeControlPanel *pcp,
                               char *execpath, char *argv[], char *envp[], int multitask,
                               pid_t *cpid)
{
	int ret;
	int startfd;
	int numev = multitask ? sysconf(_SC_NPROCESSORS_ONLN) : 1;
	pid_t tpid;
	struct probe_state *st;
	struct perf_event_attr peattr;
	struct perf_event_attr *ppeattr = &peattr;
	struct PerfevResult r[numev];
	struct PerfevChildArgs cargs;

	if (probeout_check_size(pout)) {
		errno = EINVAL;
		return DRAMLOAD_ERR_POUT;
	}

	memset(ppeattr, 0, sizeof(*ppeattr));
	if (setup_perfev(&peattr, multitask)) {
		return DRAMLOAD_ERR_PERFEV;
	}

	cargs.exec_path = execpath;
	cargs.argv = argv;
	cargs.envp = envp;
	cargs.flags = PERFEV_FLAG_STRICT;
	if (multitask) {
		cargs.flags |= PERFEV_FLAG_GROUP;
		cargs.process_events = NULL;
		cargs.num_proc_ev = 0;
		cargs.percpu_events = &ppeattr;
		cargs.num_percpu_ev = 1;
	} else {
		cargs.process_events = &ppeattr;
		cargs.num_proc_ev = 1;
		cargs.percpu_events = NULL;
		cargs.num_percpu_ev = 0;
	}

	tpid = perfev_child_spawn_delayed(&cargs, r, &startfd);
	if (tpid < 0) {
		return tpid;
	}
	if ((ret = setup_state(&st, tpid, r, numev, pout, pcp))) {
		return ret;
	}
	st->pst.tstart_fd = startfd;

	if (pthread_create(&st->pst.st.pollster_thread, NULL,
	                   pollster_run_f, st->pst.st.pctx)) {
		destroy_f(st);
		return DRAMLOAD_ERR_POLLSTER;
	}

	if (cpid != NULL) {
		*cpid = tpid;
	}
	st->pst.st.status |= PROBE_STATUS_STARTED;
	return 0;
}

int probe_dramload_setup_pid(struct ProbeOutput *pout, struct ProbeControlPanel *pcp,
                             pid_t target_pid, int multitask)
{
	int ret;
	int numev = multitask ? sysconf(_SC_NPROCESSORS_ONLN) : 1;
	int aflags = PERFEV_FLAG_STRICT;
	if (multitask) {
		aflags |= PERFEV_FLAG_PERCPU | PERFEV_FLAG_GROUP;
	}
	struct probe_state *st;
	struct perf_event_attr peattr;
	struct perf_event_attr *ppeattr = &peattr;
	struct PerfevResult r[numev];

	if (probeout_check_size(pout)) {
		errno = EINVAL;
		return DRAMLOAD_ERR_POUT;
	}

	memset(ppeattr, 0, sizeof(*ppeattr));
	if (setup_perfev(&peattr, multitask)) {
		return DRAMLOAD_ERR_PERFEV;
	}

	if (perfev_attach_pid(&ppeattr, 1, aflags, target_pid, -1, r) != numev) {
		return DRAMLOAD_ERR_ATTACH;
	}
	if ((ret = setup_state(&st, target_pid, r, numev, pout, pcp))) {
		return ret;
	}
	st->pst.tstart_fd = -1;

	if (pthread_create(&st->pst.st.pollster_thread, NULL,
	                   pollster_run_f, st->pst.st.pctx)) {
		destroy_f(st);
		return DRAMLOAD_ERR_POLLSTER;
	}

	st->pst.st.status |= PROBE_STATUS_STARTED | PROBE_STATUS_TARGET_STARTED |
	                     PROBE_STATUS_TARGET_RUNNING;
	return 0;
}
