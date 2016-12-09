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
 * Common subroutines used by Linux perf_event probes
 *
 * The design shared by perf_event probes is using a perfev-util/pollster thread
 * to check on one or more perf_event file descriptors and trigger callbacks
 * that process the events into a stream of physical addresses, timing info, and
 * optionally other stuff.
 *
 * Controlling such a setup will invariably involve a lot of common code.
 * This is where this module comes in.
 */

#ifndef _HAMTIME_PROBE_PERFEV_COMMON_H
#define _HAMTIME_PROBE_PERFEV_COMMON_H 1

#include "../probe.h"

#include "perfev-util/pollster.h"

#include <unistd.h>
#include <pthread.h>

//~ #define PERFEV_COMMON_DEBUG


/*
 * Basic state for a generic perfev probe
 * Can be extended by nesting it as the first element in a custom state struct
 */
struct perfevprobe_state {
	pthread_t pollster_thread;	/* Thread running a perfev pollster */
	struct PollsterCtx *pctx;	/* Pollster context */
	struct ProbeOutput *out;	/* Probe output */

	int status;	/* Status flags used by PROBE_STATUS */
};

/*
 * Basic state for a probe that monitors performance events of a target process.
 * Can be extended by nesting it as the first element in a custom state struct
 */
struct perfevprobe_pid_state {
	struct perfevprobe_state st;
	pid_t tpid; 	/* PID of the target process */
	int tstart_fd;	/* File descriptor provided by perfev_child_spawn_delayed */
};

/*
 * Function used to decode a sample perf_event_record into a physical address
 * (pa) and optionally (if arguments aren't NULL) a virtual address (va) and
 * MemOpStats (ostat)
 *
 * For more information on perf_event_record formats see `man 2 perf_event_open'
 */
typedef int
(*sample_decode_func_t)(void *arg, void *record,
                        uint64_t *pa, uint64_t *va, struct MemOpStats *ostat);

/* Function used to handle a non-sample perf_event_record */
typedef void
(*record_handle_func_t)(void *arg, void *record);

/*
 * END callback to be passed to pollster. Ensures that out->finished is set and
 * that consumers are notified via the mutex/cond combo.
 */
void perfevprobe_end_cb(void *arg);

/*
 * IN callback template for processing sample perf_event records.
 *
 * Reads from the MMAP buffer one entry at a time, passes records of type
 * PERF_RECORD_SAMPLE on to sample_decode and writes its output to the stream
 * at pout.
 *
 * This is intended to be called from a wrapper function that gets passed
 * as the actual IN callback to a pollster.
 */
void
perfevprobe_sample_cb(int64_t time, /* Time update value, see below */
                      struct PerfMMAP *mmap,
                      struct ProbeOutput *pout,
                      int tstamp, /* Nonzero if time is timestamp, otherwise timedelta */
                      sample_decode_func_t sample_decode,
                      void *decode_arg, /* First argument to sample_decode */
                      record_handle_func_t record_handler,
                      void *rec_arg); /* First argument to record_handler */

/* Generic control panel fuctions */
int perfevprobe_status_f(void *c);
int perfevprobe_pid_status_f(void *c);
int perfevprobe_destroy_f(void *c);
int perfevprobe_pid_destroy_f(void *c);
int perfevprobe_start_f(void *c);
int perfevprobe_stop_f(void *c);
int perfevprobe_pause_f(void *c);
int perfevprobe_pause_group_f(void *c); /* Assumes events are grouped together;
                                         * only deactivates first event fd */
int perfevprobe_resume_f(void *c);
int perfevprobe_resume_group_f(void *c); /* Assumes events are grouped together;
                                          * only reactivates first event fd */
int perfevprobe_pid_tstart_f(void *c);
int perfevprobe_pid_tstop_f(void *c);
int perfevprobe_pid_tpause_f(void *c);
int perfevprobe_pid_tresume_f(void *c);
/* Conveniently setup the generic control panel functions */
void perfevprobe_setup_cpfuncs(struct ProbeControlPanel *pcp);
void perfevprobe_pid_setup_cpfuncs(struct ProbeControlPanel *pcp);

/* Open and return a file descriptor to the pagemap file of a particular PID */
int get_pagemap_fd(pid_t pid);

#endif /* common.h */
