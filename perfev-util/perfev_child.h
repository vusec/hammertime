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

/* Spawn a child process and attach performance events to it before execve() */

#ifndef _HAMTIME_PERFEV_CHILD_H
#define _HAMTIME_PERFEV_CHILD_H 1

#include "perfev.h"

#include <unistd.h>

struct PerfevChildArgs {
	char *exec_path;
	char **argv;
	char **envp;
	struct perf_event_attr **process_events; /* Events to attach to child as a whole */
	struct perf_event_attr **percpu_events; /* Events to attach to child on a per-CPU basis */
	int num_proc_ev; /* Length of the above array */
	int num_percpu_ev; /* ditto */
	int flags; /* Flags passed to perfev_attach_pid */
};

#define PERFEV_CHILD_ERR_UNDEF		-1 /* None of the below */
#define PERFEV_CHILD_ERR_POPEN		-2 /* Error opening pipe */
#define PERFEV_CHILD_ERR_PCLOSE		-4 /* Error closing pipe */
#define PERFEV_CHILD_ERR_PIPEIO		-8 /* Error in pipe I/O */
#define PERFEV_CHILD_ERR_KILL		-16 /* Error killing child in response to other error */
#define PERFEV_CHILD_ERR_EVATTACH	-32 /* Error attaching all events to child */

/* Spawn a child, attach events and perform execve immediately */
pid_t perfev_child_spawn(struct PerfevChildArgs *args, struct PerfevResult *res);
/*
 * Spawn a child, attach events, but delay execve until a byte is written into
 * child_start_fd. Closing this fd before writing will cause the child to exit.
 */
pid_t perfev_child_spawn_delayed(struct PerfevChildArgs *args,
                                 struct PerfevResult *res, int *child_start_fd);

#endif /* perfev_child.h */
