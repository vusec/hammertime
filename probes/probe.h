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
 * Hammertime probe interface
 *
 * A probe is the piece of software that "taps" into a source providing memory
 * operation events (e.g. hardware performance counters, instrumented execution
 * environments, etc.) and presents these operations in a common format
 * understood by the rest of the toolchain.
 * This format consists of a stream of 2 types of entries:
 *  	- memory operation entries (i.e. loads and stores)
 *  		containing primarily the physical address of the op
 *  	- time value update entries
 *  		expressing the relative passage of time within the stream
 *
 * Optionally, through the cunning use of flags, memory operation entries can
 * provide additional information about the op, including its type and source
 * process.
 */

#ifndef _HAMTIME_PROBE_H
#define _HAMTIME_PROBE_H 1

#include <stdint.h>

#include <unistd.h>
#include <pthread.h>

struct MemOpStats {
	pid_t pid; /* PID of the process that generated this memory operation */
	uint32_t isstore:1; /* Set if op is a store; clear if a load */
	uint32_t reserved:7; /* Reserved for future */
	uint32_t custflags:24; /* Probe-specific flags */
};

#define PROBEOUT_VIRTADDR	1 /* Include the virtual address of mem ops */
#define PROBEOUT_OPSTATS	2 /* Include a MemOpStats entry along mem ops */

struct ProbeOutput {
	/* Fields set by the probe caller */

	size_t data_size; /* Data buffer size, in bytes */
	void *data; /* Data buffer, to be written to by the probe */
	int fmtflags; /* Flags indicating the expected format of entries in *data */
	/*
	 * NOTE: format of entries in *data
	 * Depending on fmtflags, memory operation entries are in the form
	 *
	 * struct memop_entry {
	 *  	uint64_t physical_address;
	 *  	uint64_t virtual_address;	(if (fmtflags & PROBEOUT_VIRTADDR))
	 *  	struct MemOpStats stats;	(if (fmtflags & PROBEOUT_OPSTATS))
	 * }
	 *
	 * Regardless of fmtflags, time value update entries have a special format
	 *
	 * struct timev_entry {
	 *  	uint64_t physical_address == -1;
	 *  	int64_t timev;
	 * }
	 * A positive timev indicates a timestamp, while a negative indicates a time
	 * delta. In both cases, time is expressed in units of nanoseconds.
	 *
	 * memop and timev entries can be freely interspersed in the output stream.
	 * Therefore, a reader (consumer) should ALWAYS check the value of
	 * physical_address.
	 */

	/* Synchronization variables used by the probe to signal new data */
	pthread_cond_t update_cond;
	pthread_mutex_t update_mutex;

	/* Fields set/updated by the probe itself */

	uint64_t head; /* Head of output data, in bytes; does not wrap at data_size */
	uint8_t finished; /* Flag to indicate that no more data shall be generated */

	/* A simple linear scale to indicate what proportion of "true" memory ops
	 * are estimated lost through sampling. 0 = 0%; 255 = 255/256 ~ 99.6% */
	uint8_t sample_loss;
};

/*
 * Sanity check that an entry will always fall within a contiguous buffer region.
 * Greatly simplifies producer and consumer code.
 */
static inline int probeout_check_size(struct ProbeOutput *pout)
{
	size_t sz = pout->data_size;
	if (sz % sizeof(uint64_t)) {
		return 1;
	}
	if ((pout->fmtflags & PROBEOUT_OPSTATS) &&
	    (pout->data_size % sizeof(struct MemOpStats)))
	{
		return 1;
	}
	return 0;
}

/*
 * The correct way to check (and wait on) an output head update.
 * Returns 1 if output finished, 0 otherwise.
 */
static inline int probeout_read_head(struct ProbeOutput *pout,
                                     uint64_t cur, uint64_t *head)
{
	uint64_t nhead;
	pthread_mutex_lock(&pout->update_mutex);
	nhead = pout->head;
	if (nhead == cur) {
		if (pout->finished) {
			pthread_mutex_unlock(&pout->update_mutex);
			return 1;
		}
		pthread_cond_wait(&pout->update_cond, &pout->update_mutex);
		nhead = pout->head;
	}
	pthread_mutex_unlock(&pout->update_mutex);
	*head = nhead;
	return 0;
}

enum ProbeCPFunc {
	PROBE_STATUS,		/* Retrieve status of probe */
	PROBE_DESTROY,		/* Clean-up any data structures associated with probe
	              		 * NOTE: DOES NOT terminate probe or target, you must
	              		 * ensure of that yourself. You have been warned. */
	PROBE_START,		/* Start probe; does not also start target if present */
	PROBE_STOP,			/* Terminate probe; IMPORTANT SEE BELOW */
	PROBE_PAUSE,		/* Pause probe (this should also pause data generation) */
	PROBE_RESUME,		/* Resume a paused probe */
	PROBE_TARGET_START,	/* Start the target, if applicable */
	PROBE_TARGET_STOP,	/* Permanently terminate target */
	PROBE_TARGET_PAUSE,	/* Pause target execution, if possible */
	PROBE_TARGET_RESUME,/* Resume a paused target */

	_PROBE_CPMAXF		/* Guard value; used for allocations, etc. */
};

/*
 * IMPORTANT: PROBE TERMINATION
 * A probe implementation MUST make sure to signal all probe output consumers
 * (by using the cond in struct ProbeOutput) during normal termination AFTER
 * data generation has stopped and the `finished' flag is set in ProbeOutput.
 */

/*
 * Control panel funcs (with the exception of PROBE_STATUS, described below)
 * must return the following codes on success or if not implemented, respectively.
 * Probes are free to assign error codes to other integer values
 */
#define PROBE_CPFUNC_SUCCESS  0
#define PROBE_CPFUNC_NOTIMPL -1

/* PROBE_STATUS control panel function exit flags */
#define PROBE_STATUS_STARTED			1 /* Probe has been initialized and started */
#define PROBE_STATUS_TARGET_STARTED		2 /* Target has been initialized and started */
#define PROBE_STATUS_RUNNING			4 /* Probe running */
#define PROBE_STATUS_TARGET_RUNNING		8 /* Target running */
#define PROBE_STATUS_TERMINATED			16 /* Probe has terminated; no more data shall be produced */
#define PROBE_STATUS_TARGET_TERMINATED	32 /* Target has terminated execution */

typedef int (*probe_cpfunc_t)(void *ctx);

struct ProbeControlPanel {
	void *ctx; /* Opaque pointer to be passed to the control panel funcs */
	int (*custom)(void *ctx, void *arg); /* Custom, probe-specific func, if any */
	probe_cpfunc_t func[_PROBE_CPMAXF]; /* Standard probe control funcs */
};

/*
 * WARNING: calling PROBE_DESTROY will free allocated data structures associated
 * with the probe, including, but not limited to, ProbeControlPanel.ctx .
 * It is essentially a poor-man's destructor
 * DO NOT attempt to call any other control panel funcs after PROBE_DESTROY
 */

#endif /* probe.h */
