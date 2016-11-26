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

#include "pollster.h"

#include <string.h>
#include <stdio.h>

#include <unistd.h>
#include <time.h>
#include <poll.h>
#include <sys/mman.h>

#include <linux/perf_event.h>

#ifndef rmb
#if (defined(__x86_64__) | defined(__i386__))
#define rmb() asm volatile("lfence":::"memory")
#endif
#endif

/*
 * Ideally, poll would run completely asynchronously, with infinite timeout.
 * Unfortunately, we don't get notified when an fd is being closed so we need
 * to periodically wake up and check whether we still have valid fds.
 */
#define POLL_TIMEOUT_MS 2000

struct fdinfo {
	struct timespec atime;
	void *mmap;
	struct PerfMMAP mmap_info;
};

static const struct timespec TZERO = {0, 0};

static inline int64_t timedelta(struct timespec t0, struct timespec t)
{
	return ((t.tv_sec - t0.tv_sec) * 1000000000) + (t.tv_nsec - t0.tv_nsec);
}

void pollster_run(struct PollsterCtx *c)
{
	struct timespec t;
	long page_size = sysconf(_SC_PAGESIZE);
	unsigned int nfd = c->num_fds;
	unsigned int active_fds = nfd;
	struct pollfd pfds[nfd];
	struct fdinfo stat[nfd];
	memset(stat, 0, nfd * sizeof(*stat));

	/* Setup local bookeeping data */
	clock_gettime(CLOCK_REALTIME, &t);
	for (unsigned int i = 0; i < nfd; i++) {
		pfds[i].fd = c->fds[i].fd;
		pfds[i].events = POLLIN;
		pfds[i].revents = 0;

		stat[i].atime = t;
		if (c->fds[i].mmap_pages) {
			size_t sz = (c->fds[i].mmap_pages + 1) * page_size;
			void *map = mmap(NULL, sz, PROT_READ, MAP_SHARED, c->fds[i].fd, 0);
			if (map == MAP_FAILED) {
				perror("Pollster MMAP error");
				goto unmap_all;
			}
			stat[i].mmap = map;
			stat[i].mmap_info.data = (uint8_t *)map + page_size;
			stat[i].mmap_info.data_size = sz - page_size;
			stat[i].mmap_info.head = 0;
			stat[i].mmap_info.old_head = 0;
		}
	}

	while (active_fds && !(volatile int)c->terminate) {
		int ready = poll(pfds, nfd, POLL_TIMEOUT_MS);
		if (ready < 0) {
			perror("Poll syscall error");
			break;
		}
		clock_gettime(CLOCK_REALTIME, &t);
		/* Update MMAP structures */
		for (int i = 0, act = ready; i < nfd && act; i++) {
			if (pfds[i].fd >= 0 && (pfds[i].revents & POLLIN) && stat[i].mmap) {
				act--;
				uint64_t newhead = ((volatile struct perf_event_mmap_page *)stat[i].mmap)->data_head;
				rmb();
				stat[i].mmap_info.old_head = stat[i].mmap_info.head;
				stat[i].mmap_info.head = newhead;
			}
		}
		/* Do callbacks */
		for (int i = 0, act = ready; i < nfd && act; i++) {
			int curfd = pfds[i].fd;
			if (curfd >= 0 && pfds[i].revents) {
				act--;
				if (pfds[i].revents & POLLIN) {
					//~ printf("%d ", pfds[i].revents);
					int64_t timev = timedelta(
					                (c->fds[i].fl_timestamp) ? TZERO : stat[i].atime,
					                t);
					c->fds[i].callback(curfd, timev,
					                   &stat[i].mmap_info, c->fds[i].arg);
				} else {
					int ans = (c->misc_callback != NULL)
					          ? (c->misc_callback(curfd, pfds[i].revents, c->misc_arg))
					          : 0;
					if (!ans) {
						pfds[i].fd = -curfd; /* Mask out fd */
						if (c->flags & POLLSTER_FLAG_STRICT) {
							active_fds = 0;
						} else {
							active_fds--;
						}
					}
				}
				pfds[i].revents = 0;
				stat[i].atime = t;
			}
		}
	}

	unmap_all:
	for (unsigned int i = 0; i < nfd; i++) {
		if (stat[i].mmap != NULL) {
			munmap(stat[i].mmap, stat[i].mmap_info.data_size + page_size);
		}
	}
	/* Normal (or not) exit */
	if (c->end_callback != NULL) {
		c->end_callback(c->end_arg);
	}
	return;
}

void *pollster_run_f(void *arg)
{
	pollster_run((struct PollsterCtx *)arg);
	return NULL;
}
