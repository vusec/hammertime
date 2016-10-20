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

#include "common.h"

#include <alloca.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <signal.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/perf_event.h>

#define RECORD_BUFSZ	64

void perfevprobe_end_cb(void *arg)
{
	struct ProbeOutput *o = (struct ProbeOutput *)arg;
	pthread_mutex_lock(&o->update_mutex);
	o->finished = 1;
	pthread_cond_broadcast(&o->update_cond);
	pthread_mutex_unlock(&o->update_mutex);
}

void perfevprobe_sample_cb(int64_t time,
                           struct PerfMMAP *mmap,
                           struct ProbeOutput *o,
                           int tstamp,
                           sample_decode_func_t sample_decode,
                           void *decode_arg,
                           record_handle_func_t record_handler,
                           void *rec_arg)
{
	uint8_t *dp = mmap->data;
	uint64_t sz = mmap->data_size;
	uint8_t *odp = o->data;
	uint64_t osz = o->data_size;
	uint64_t ohead = o->head;

	uint64_t pa, va;
	struct MemOpStats opst;
	uint64_t *vap = (o->fmtflags & PROBEOUT_VIRTADDR) ? &va : NULL;
	struct MemOpStats *opstp = (o->fmtflags & PROBEOUT_OPSTATS) ? &opst : NULL;

	struct perf_event_header *pr;
	uint8_t *rbuf = alloca(RECORD_BUFSZ);

	uint64_t dist = mmap->head - mmap->old_head;
	//~ printf("%6lx\n", dist);
	if (dist > sz/2) {
		//~ puts("PERF BUF WARN!");
	}
	if (dist > sz) {
		//~ puts("PERF BUF LOSS!!!");
		//~ printf("OH: %10lx; CH: %10lx; D: %10lx\n", mmap->old_head, mmap->head, mmap->head - mmap->old_head);
		return;
	}

	for (uint64_t cur = mmap->old_head; cur < mmap->head; cur += pr->size) {
		pr = (struct perf_event_header *)(dp + (cur % sz));
		if ((uint32_t)pr->type > PERF_RECORD_MAX) {
			/* Rubbish data; most likely de-synced; abort */
			//~ printf("C: %ld; S: %ld; ", (cur % sz), sz);
			//~ printf("PS: %d; T: %d\n", pr->size, (uint32_t)pr->type);
			return;
		}
		int diff = ((cur % sz) + pr->size) - sz;
		if (diff > 0) {
			//~ printf("Circular buffer wraparound by %d bytes (%d byte struct)\n", diff, pr->size);
			if (pr->size <= RECORD_BUFSZ) {
				memcpy(rbuf, pr, pr->size - diff);
				memcpy(rbuf + pr->size - diff, dp, diff);
				pr = (struct perf_event_header *)rbuf;
			} else {
				//~ puts("Circular buffer skip!");
			}
		}
		if (pr->type == PERF_RECORD_SAMPLE) {
			if (!sample_decode(decode_arg, pr, &pa, vap, opstp)) {
				*((uint64_t *)(odp + (ohead % osz))) = pa;
				ohead += sizeof(uint64_t);
				if (vap) {
					*((uint64_t *)(odp + (ohead % osz))) = va;
					ohead += sizeof(uint64_t);
				}
				if (opstp) {
					*((struct MemOpStats *)(odp + (ohead % osz))) = opst;
					ohead += sizeof(struct MemOpStats);
				}
			}
		} else if (record_handler != NULL) {
			record_handler(rec_arg, pr);
		}
	}
	*((uint64_t *)(odp + (ohead % osz))) = (uint64_t)-1;
	ohead += sizeof(uint64_t);
	*((int64_t *)(odp + (ohead % osz))) = (tstamp) ? time : -time;
	ohead += sizeof(uint64_t);

	pthread_mutex_lock(&o->update_mutex);
	o->head = ohead;
	pthread_cond_broadcast(&o->update_cond);
	pthread_mutex_unlock(&o->update_mutex);
}

int perfevprobe_status_f(void *c)
{
	struct perfevprobe_state *s = (struct perfevprobe_state *)c;
	if (s->out->finished) {
		s->status &= ~PROBE_STATUS_RUNNING;
		s->status |= PROBE_STATUS_TERMINATED;
	}
	return s->status;
}
int perfevprobe_pid_status_f(void *c)
{
	struct perfevprobe_pid_state *s = (struct perfevprobe_pid_state *)c;
	if (s->st.out->finished) {
		s->st.status &= ~PROBE_STATUS_RUNNING;
		s->st.status |= PROBE_STATUS_TERMINATED;
		if (!(s->st.status & PROBE_STATUS_TARGET_TERMINATED)) {
			/* Probe has terminated, but we don't know about the target; check */
			if (kill(s->tpid, 0)) {
				s->st.status &= ~PROBE_STATUS_TARGET_RUNNING;
				s->st.status |= PROBE_STATUS_TARGET_TERMINATED;
			}
		}
	}
	return s->st.status;
}

int perfevprobe_destroy_f(void *c)
{
	struct perfevprobe_state *s = (struct perfevprobe_state *)c;
	for (unsigned int i = 0; i < s->pctx->num_fds; i++) {
		int fd = s->pctx->fds[i].fd;
		if (fd >= 0) {
			close(fd);
		}
	}
	free(s->pctx->fds);
	free(s->pctx);
	free(c);
	return PROBE_CPFUNC_SUCCESS;
}
int perfevprobe_pid_destroy_f(void *c)
{
	struct perfevprobe_pid_state *s = (struct perfevprobe_pid_state *)c;
	if (s->tstart_fd >= 0) {
		close(s->tstart_fd);
	}
	return perfevprobe_destroy_f(c);
}

/* PROBE_START is a noop; a performance event is always present in the kernel
 * once set up, therefore the probe is always started, albeit not always running
 */
int perfevprobe_start_f(void *c)
{
	struct perfevprobe_state *s = (struct perfevprobe_state *)c;
	if (s->out->finished) {
		return 1;
	} else {
		return PROBE_CPFUNC_SUCCESS;
	}
}
int perfevprobe_stop_f(void *c)
{
	struct perfevprobe_state *s = (struct perfevprobe_state *)c;
	if (!s->out->finished) {
		perfevprobe_pause_f(c);
		s->pctx->terminate = 1;
		pthread_join(s->pollster_thread, NULL);
		for (unsigned int i = 0; i < s->pctx->num_fds; i++) {
			int fd = s->pctx->fds[i].fd;
			if (fd >= 0) {
				close(fd);
			}
		}
	}

	return PROBE_CPFUNC_SUCCESS;
}

int perfevprobe_pause_f(void *c)
{
	int ret = PROBE_CPFUNC_SUCCESS;
	struct perfevprobe_state *s = (struct perfevprobe_state *)c;
	if (s->out->finished) {
		ret = 1;
	} else {
		for (unsigned int i = 0; i < s->pctx->num_fds; i++) {
			if (ioctl(s->pctx->fds[i].fd, PERF_EVENT_IOC_DISABLE, NULL)) {
				ret |= 2 << i;
			}
		}
		if (!ret) {
			s->status &= ~PROBE_STATUS_RUNNING;
		}
	}
	return ret;
}
int perfevprobe_pause_group_f(void *c)
{
	int ret = PROBE_CPFUNC_SUCCESS;
	struct perfevprobe_state *s = (struct perfevprobe_state *)c;
	if (s->out->finished) {
		ret = 1;
	} else {
		ret = ioctl(s->pctx->fds[0].fd, PERF_EVENT_IOC_DISABLE, NULL);
		if (!ret) {
			s->status &= ~PROBE_STATUS_RUNNING;
		}
	}
	return ret;
}

int perfevprobe_resume_f(void *c)
{
	int ret = PROBE_CPFUNC_SUCCESS;
	struct perfevprobe_state *s = (struct perfevprobe_state *)c;
	if (s->out->finished) {
		ret = 1;
	} else {
		for (unsigned int i = 0; i < s->pctx->num_fds; i++) {
			if (ioctl(s->pctx->fds[i].fd, PERF_EVENT_IOC_ENABLE, NULL)) {
				ret |= 2 << i;
			}
		}
		if (!ret) {
			s->status |= PROBE_STATUS_RUNNING;
		}
	}
	return ret;
}
int perfevprobe_resume_group_f(void *c)
{
	int ret = PROBE_CPFUNC_SUCCESS;
	struct perfevprobe_state *s = (struct perfevprobe_state *)c;
	if (s->out->finished) {
		ret = 1;
	} else {
		ret = ioctl(s->pctx->fds[0].fd, PERF_EVENT_IOC_ENABLE, NULL);
		if (!ret) {
			s->status |= PROBE_STATUS_RUNNING;
		}
	}
	return ret;
}

int perfevprobe_pid_tstart_f(void *c)
{
	struct perfevprobe_pid_state *s = (struct perfevprobe_pid_state *)c;
	if ((s->st.out->finished) ||
	    (s->st.status & PROBE_STATUS_TARGET_TERMINATED))
	{
		return 1;
	} else if (s->st.status & PROBE_STATUS_TARGET_STARTED) {
		return PROBE_CPFUNC_SUCCESS;
	} else if (s->tstart_fd >= 0) {
		char v = '\0';
		if (write(s->tstart_fd, &v, 1) == 1) {
			s->st.status |= PROBE_STATUS_TARGET_STARTED | PROBE_STATUS_TARGET_RUNNING;
			close(s->tstart_fd);
			s->tstart_fd = -1;
			return PROBE_CPFUNC_SUCCESS;
		} else {
			return 2;
		}
	} else {
		return PROBE_CPFUNC_NOTIMPL;
	}
}
int perfevprobe_pid_tstop_f(void *c)
{
	struct perfevprobe_pid_state *s = (struct perfevprobe_pid_state *)c;
	if (s->st.status & PROBE_STATUS_TARGET_TERMINATED) {
		return PROBE_CPFUNC_SUCCESS;
	} else {
		return kill(s->tpid, SIGTERM) ? 1 : PROBE_CPFUNC_SUCCESS;
	}
}
int perfevprobe_pid_tpause_f(void *c)
{
	struct perfevprobe_pid_state *s = (struct perfevprobe_pid_state *)c;
	if (s->st.status & PROBE_STATUS_TARGET_TERMINATED) {
		return 1;
	} else {
		int ret = kill(s->tpid, SIGSTOP) ? 1 : PROBE_CPFUNC_SUCCESS;
		if (!ret) {
			s->st.status &= ~PROBE_STATUS_TARGET_RUNNING;
		}
		return ret;
	}
}
int perfevprobe_pid_tresume_f(void *c)
{
	struct perfevprobe_pid_state *s = (struct perfevprobe_pid_state *)c;
	if (s->st.status & PROBE_STATUS_TARGET_TERMINATED) {
		return 1;
	} else {
		int ret = kill(s->tpid, SIGCONT) ? 1 : PROBE_CPFUNC_SUCCESS;
		if (!ret) {
			s->st.status |= PROBE_STATUS_TARGET_RUNNING;
		}
		return ret;
	}
}

/* Placeholder function returning PROBE_CPFUNC_NOTIMPL */
int notimpl_f(void *c) { return PROBE_CPFUNC_NOTIMPL; }

void perfevprobe_setup_cpfuncs(struct ProbeControlPanel *pcp)
{
	probe_cpfunc_t *f = pcp->func;
	f[PROBE_STATUS] = perfevprobe_status_f;
	f[PROBE_DESTROY] = perfevprobe_destroy_f;
	f[PROBE_START] = perfevprobe_start_f;
	f[PROBE_STOP] = perfevprobe_stop_f;
	f[PROBE_PAUSE] = perfevprobe_pause_f;
	f[PROBE_RESUME] = perfevprobe_resume_f;
	f[PROBE_TARGET_START] = notimpl_f;
	f[PROBE_TARGET_STOP] = notimpl_f;
	f[PROBE_TARGET_PAUSE] = notimpl_f;
	f[PROBE_TARGET_RESUME] = notimpl_f;
	pcp->custom = NULL;
}
void perfevprobe_pid_setup_cpfuncs(struct ProbeControlPanel *pcp)
{
	probe_cpfunc_t *f = pcp->func;
	f[PROBE_STATUS] = perfevprobe_pid_status_f;
	f[PROBE_DESTROY] = perfevprobe_pid_destroy_f;
	f[PROBE_START] = perfevprobe_start_f;
	f[PROBE_STOP] = perfevprobe_stop_f;
	f[PROBE_PAUSE] = perfevprobe_pause_f;
	f[PROBE_RESUME] = perfevprobe_resume_f;
	f[PROBE_TARGET_START] = perfevprobe_pid_tstart_f;
	f[PROBE_TARGET_STOP] = perfevprobe_pid_tstop_f;
	f[PROBE_TARGET_PAUSE] = perfevprobe_pid_tpause_f;
	f[PROBE_TARGET_RESUME] = perfevprobe_pid_tresume_f;
	pcp->custom = NULL;
}


#define PAGEMAP_PATHLEN 24

int get_pagemap_fd(pid_t pid)
{
	char pagemap_path[PAGEMAP_PATHLEN];
	if (snprintf(pagemap_path, PAGEMAP_PATHLEN,
	             "/proc/%d/pagemap", pid) > PAGEMAP_PATHLEN)
	{
		errno = EDOM;
		return -1;
	}
	return open(pagemap_path, O_RDONLY);
}
