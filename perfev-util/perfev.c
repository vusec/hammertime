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

#include "perfev.h"

#include <errno.h>
#include <string.h>

#include <unistd.h>
#include <sys/syscall.h>

#include <perfmon/pfmlib.h>
#include <perfmon/pfmlib_perf_event.h>

/* Syscall wrapper */
static inline int sys_perf_event_open(struct perf_event_attr *attr,
                                      pid_t pid, int cpu, int group_fd,
                                      unsigned long flags)
{
    int ret;
    ret = syscall(__NR_perf_event_open, attr, pid, cpu, group_fd, flags);
    return ret;
}

static int attach(struct perf_event_attr *events[], int num_events, int flags,
                  pid_t pid, int cpu, int group, struct PerfevResult *res)
{
	int ret = 0;
	unsigned long open_flags = ((flags & PERFEV_FLAG_CGROUP) ? PERF_FLAG_PID_CGROUP : 0) |
	                           ((flags & PERFEV_FLAG_CLOEXEC) ? PERF_FLAG_FD_CLOEXEC : 0);
	for (int i = 0; i < num_events; i++) {
		errno = 0;
		int fd = sys_perf_event_open(events[i], pid, cpu, group, open_flags);
		res[i].fd = fd;
		res[i].err = errno;
		if (fd > 0) {
			ret++;
		} else if (flags & PERFEV_FLAG_STRICT) {
			break;
		}
	}
	return ret;
}

int perfev_attach_pid_cpu(struct perf_event_attr *events[], int num_events,
                          int flags, pid_t pid, int cpu, int group_fd,
                          struct PerfevResult *res)
{
	int ret = 0;
	if (group_fd < 0) group_fd = -1;
	if (num_events > 0) {
		if (flags & PERFEV_FLAG_GROUP && group_fd < 0) {
			ret = attach(events, 1, flags, pid, cpu, -1, res);
			if ((flags & PERFEV_FLAG_STRICT) && !ret) {
				return ret;
			}
			group_fd = res[0].fd;
			events++;
			num_events--;
			res++;
		}
		ret += attach(events, num_events, flags, pid, cpu, group_fd, res);
	}
	return ret;
}

int perfev_attach_pid(struct perf_event_attr *events[], int num_events,
                      int flags, pid_t pid, int group_fd, struct PerfevResult *res)
{
	if (flags & PERFEV_FLAG_PERCPU) {
		int ret = 0;
		int numcpus = sysconf(_SC_NPROCESSORS_ONLN);
		for (int cpu = 0; cpu < numcpus; cpu++) {
			int r = perfev_attach_pid_cpu(events, num_events, flags, pid, cpu, group_fd, res);
			ret += r;
			if ((flags & PERFEV_FLAG_STRICT) && r < num_events) {
				return ret;
			}
			res += num_events;
		}
		return ret;
	} else {
		return perfev_attach_pid_cpu(events, num_events, flags, pid, -1, group_fd, res);
	}
}

int perfev_encode(const char *evstr, struct perf_event_attr *attr)
{
	static int pfm_init = 0;
	static int pfm_err = 0;
	if (!pfm_init) {
		pfm_init = 1;
		pfm_err = pfm_initialize();
	}
	if (pfm_err) {
		return pfm_err;
	}

	pfm_perf_encode_arg_t pearg = {
		.size = sizeof(pfm_perf_encode_arg_t),
		.attr = attr,
		.fstr = NULL,
	};
	return pfm_get_os_event_encoding(evstr, PFM_PLM3, PFM_OS_PERF_EVENT, &pearg);
}

int perfev_setup(char **options[], int num_events,
                 struct perf_event_attr *events[], char *picks[])
{
	int ret = 0;
	for (int idx = 0; idx < num_events; idx++) {
		if (picks != NULL) {
			picks[idx] = NULL;
		}
		for (char **opt = options[idx]; opt != NULL; opt++) {
			if (perfev_encode(*opt, events[idx]) == PFM_SUCCESS) {
				if (picks != NULL) {
					picks[idx] = *opt;
				}
				ret++;
				break;
			}
		}
	}
	return ret;
}

/* Convenience functions */
int perfev_attach_cpu(struct perf_event_attr *events[], int num_events,
                      int flags, int cpu, int group_fd, struct PerfevResult *res)
{
	return perfev_attach_pid_cpu(events, num_events, flags, -1, cpu, group_fd, res);
}

int perfev_attach_self(struct perf_event_attr *events[], int num_events,
                       int flags, int group_fd, struct PerfevResult *res)
{
	return perfev_attach_pid(events, num_events, flags, 0, group_fd, res);
}
