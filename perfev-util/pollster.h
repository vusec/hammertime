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
 * Linux perf event pollster.
 * Handles polling of an arbitrary number of perf event file descriptors,
 * using callbacks to handle events.
 */

#ifndef _HAMTIME_PERFEV_POLLSTER_H
#define _HAMTIME_PERFEV_POLLSTER_H 1

#include <stdint.h>

struct PerfMMAP {
	uint64_t head; /* Current head position, in bytes */
	uint64_t old_head; /* Value of head last time this event triggered */
	uint64_t data_size; /* Size of data area, in bytes */
	void *data; /* Circular buffer written into by the kernel */
};
/* For more details on the perf_event MMAP interface see `man 2 perf_event_open' */

/* IN callback -- called when the POLL_IN event is signaled on an fd */
typedef void (*in_callback_t)(int fd, int64_t time,
                              struct PerfMMAP *mmap, void *arg);
/*
 * MISC callback -- called when a different poll event is signaled on an fd.
 * Must return 0 if the fd should be deactivated, nonzero if it should remain
 * actively polled.
 */
typedef int (*misc_callback_t)(int fd, short revents, void *arg);
/* END callback -- called when the pollster naturally exits */
typedef void (*end_callback_t)(void *arg);

struct PollsterFd {
	int fd; /* File descriptor of perf_event */
	int mmap_pages; /* Number of pages to mmap for data area */
	in_callback_t callback;
	void *arg; /* Argument passed to the IN callback */

	/* Flags, up to 8 bits */
	uint8_t fl_timestamp:1; /* Default is to send a time delta */
};

struct PollsterCtx {
	int flags;
	int terminate;
	unsigned int num_fds;
	struct PollsterFd *fds;
	misc_callback_t misc_callback;
	end_callback_t end_callback;
	void *misc_arg;
	void *end_arg;
};

#define POLLSTER_FLAG_STRICT	1 /* Exit on first fd deactivation */

void pollster_run(struct PollsterCtx *c);
void *pollster_run_f(void *arg); /* pthread-compatible start routine */

#endif /* pollster.h */
