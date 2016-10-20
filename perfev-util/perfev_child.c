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

#define _GNU_SOURCE

#include "perfev_child.h"

#include <stdlib.h>
#include <stdio.h>

#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

static void child_exec(struct PerfevChildArgs *args, int waitpipe[2])
{
	char v;
	close(waitpipe[1]);
	/* Wait for parent to attach performance events */
	if (read(waitpipe[0], &v, 1) != 1) {
		perror("Waitpipe read error");
		exit(2);
	}
	close(waitpipe[0]);
	/* Go ahead with exec */
	if (execvpe(args->exec_path, args->argv, args->envp)) {
		perror("Exec failed");
		exit(1);
	}
}

static pid_t parent_exec(struct PerfevChildArgs *args, struct PerfevResult *res,
                         int waitpipe[2])
{
	pid_t child = fork();
	if (child) {
		pid_t ret = 0;

		if (close(waitpipe[0])) {
			ret = PERFEV_CHILD_ERR_PCLOSE;
			goto kill_child;
		}

		/* Attach events to child */
		int openev;
		openev = perfev_attach_pid(args->process_events, args->num_proc_ev,
		                           args->flags & ~PERFEV_FLAG_PERCPU,
		                           child, -1, res);
		if ((args->flags & PERFEV_FLAG_STRICT) && openev < args->num_proc_ev) {
			ret = PERFEV_CHILD_ERR_EVATTACH;
			goto kill_child;
		}
		res += args->num_proc_ev;
		openev = perfev_attach_pid(args->percpu_events, args->num_percpu_ev,
		                           args->flags | PERFEV_FLAG_PERCPU,
		                           child, -1, res);
		if ((args->flags & PERFEV_FLAG_STRICT) &&
		    openev < args->num_percpu_ev * sysconf(_SC_NPROCESSORS_ONLN))
		{
			ret = PERFEV_CHILD_ERR_EVATTACH;
			goto kill_child;
		}

		return child;

		/* Something went wrong; kill child and bail out */
		kill_child:
		if (kill(child, SIGKILL)) {
			ret += PERFEV_CHILD_ERR_KILL;
		} else {
			waitpid(child, NULL, 0);
		}
		return ret;
	} else {
		child_exec(args, waitpipe);
	}
	/* Should never reach */
	return PERFEV_CHILD_ERR_UNDEF;
}

pid_t perfev_child_spawn(struct PerfevChildArgs *args, struct PerfevResult *res)
{
	pid_t child;
	char v = '\0';
	int waitpipe[2];
	if (pipe(waitpipe)) {
		return PERFEV_CHILD_ERR_POPEN;
	}

	child = parent_exec(args, res, waitpipe);
	if (child > 0) {
		pid_t ret;
		/* Signal child to proceed */
		if (write(waitpipe[1], &v, 1) != 1) {
			ret = PERFEV_CHILD_ERR_PIPEIO;
			if (kill(child, SIGKILL)) {
				ret += PERFEV_CHILD_ERR_KILL;
			} else {
				waitpid(child, NULL, 0);
			}
			child = ret;
		}
	}
	close(waitpipe[1]);
	return child;
}

pid_t perfev_child_spawn_delayed(struct PerfevChildArgs *args, struct PerfevResult *res,
                                 int *child_start_fd)
{
	pid_t child;
	int waitpipe[2];
	if (pipe(waitpipe)) {
		return PERFEV_CHILD_ERR_POPEN;
	}

	child = parent_exec(args, res, waitpipe);
	if (child > 0) {
		*child_start_fd = waitpipe[1];
	} else {
		close(waitpipe[1]);
	}
	return child;
}
