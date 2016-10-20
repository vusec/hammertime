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

/* Utility functions for encoding and attaching performance events */

#ifndef _HAMTIME_PERFEV_H
#define _HAMTIME_PERFEV_H 1

#include <unistd.h>
#include <linux/perf_event.h>

struct PerfevResult {
	int fd; /* File descriptor of opened event */
	int err; /* errno set by perf_event_open in case of error */
};

#define PERFEV_FLAG_GROUP	1 /* Group events together; leader is the first event opened */
#define PERFEV_FLAG_STRICT	2 /* All-or-nothing approach; fail early at first event open error */
#define PERFEV_FLAG_CGROUP	4 /* Pass the PERF_FLAG_PID_CGROUP flag to the kernel */
#define PERFEV_FLAG_CLOEXEC	8 /* Pass the PERF_FLAG_FD_CLOEXEC flag to the kernel */

/* Passed to perfev_attach_pid and perfev_attach_self indicates that separate
 * event fds should be opened for each individual active CPU on the system.
 * This is required for inherited sampling events, otherwise MMAP will fail.
 * Useful for multi-task (i.e. multi-[threaded|process]) monitoring.
 * Multiplies the number of results output by the number of online CPUs.
 * If PERFEV_FLAG_GROUP is also passed, creates an event group for each CPU.
 */
#define PERFEV_FLAG_PERCPU	16

/* Perf event attach functions.
 * Writes details into *res, returns the number of events successfully attached.
 *
 * group, if non-negative, acts as group override for perf_event_open calls.
 * PERFEV_FLAG_GROUP is ignored in this case.
 */
int perfev_attach_pid_cpu(struct perf_event_attr *events[], int num_events,
                          int flags, pid_t pid, int group, int cpu,
                          struct PerfevResult *res);
int perfev_attach_pid(struct perf_event_attr *events[], int num_events,
                      int flags, pid_t pid, int group, struct PerfevResult *res);
int perfev_attach_cpu(struct perf_event_attr *events[], int num_events,
                      int flags, int cpu, int group, struct PerfevResult *res);
int perfev_attach_self(struct perf_event_attr *events[], int num_events,
                       int flags, int group, struct PerfevResult *res);

/* Encode a perf event specified by evstr.
 * Return 0 if successful, non-zero and sets errno otherwise.
 */
int perfev_encode(const char *evstr, struct perf_event_attr *attr);

/* Encode num_events perf events specified by the strings in options[].
 * options[] is an array of size num_events containing NULL-terminated arrays
 * of strings.
 * Encoded perf events will be written to events[].
 * picks[], if not NULL, will be populated with the picked entries from
 * options[].
 *
 * Example options[]
 * int num_events = 3;
 * char **options[num_events] = {
 * 	{"Preferred1", "Fallback1", NULL},
 * 	{"Preferred2", "Fallback2", "foobar", NULL},
 * 	{"Preferred3", NULL}
 * };
 */
int perfev_setup(char **options[], int num_events,
                 struct perf_event_attr *events[], char *picks[]);

#endif /* perfev.h */
