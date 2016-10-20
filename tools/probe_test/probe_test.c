/*
 * Copyright (c) 2016 Andrei Tatar
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
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

/* Small demo application that monitors a probe for a short while */

#include "probe_logger.h"

#include "probes/probe.h"
#include "probes/perfev/dram_load.h"
#include "probes/perfev/dram_load_mp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <pthread.h>


void *log_thread(void *arg)
{
	struct ProbeOutput *pout = (struct ProbeOutput *)arg;
	return (void *)(uintptr_t)log_probe_timing(pout, stderr);
}

int main(int argc, char *argv[])
{
	int r;
	const size_t BUFSIZE = 1 << 20;

	if (argc < 2) {
		printf("usage: %s <PID>\n", argv[0]);
		return 1;
	}
	pid_t tpid = atoi(argv[1]);

	struct ProbeControlPanel cp;
	struct ProbeOutput pout;
	pthread_t logthread;
	memset(&pout, 0, sizeof(pout));

	pout.fmtflags = 0;
	pout.data = malloc(BUFSIZE);
	if (pout.data == NULL) {
		perror("Mem alloc error");
		return 1;
	}
	pout.data_size = BUFSIZE;
	pthread_mutex_init(&pout.update_mutex, NULL);
	pthread_cond_init(&pout.update_cond, NULL);

	if (tpid > 0) {
		r = probe_dramload_setup_pid(&pout, &cp, tpid, 0);
	} else {
		r = probe_dramload_setup_sys(&pout, &cp);
	}

	if (r) {
		printf("ecode: %d\n", r);
		perror("Probe setup error");
		return 1;
	}

	if (pthread_create(&logthread, NULL, log_thread, &pout)) {
		perror("Log error");
		return 1;
	}

	printf("S: %d\n", cp.func[PROBE_STATUS](cp.ctx));
	if (cp.func[PROBE_START](cp.ctx)) {
		perror("Probe start error");
		return 1;
	}
	puts("Start");
	printf("S: %d\n", cp.func[PROBE_STATUS](cp.ctx));
	if (cp.func[PROBE_RESUME](cp.ctx)) {
		perror("Probe resume error");
		return 1;
	}
	puts("Resume");
	printf("S: %d\n", cp.func[PROBE_STATUS](cp.ctx));
	sleep(5);

	sleep(5);
	//~ if (cp.func[PROBE_PAUSE](cp.ctx)) {
		//~ perror("Probe pause error");
		//~ return 1;
	//~ }
	//~ puts("Pause");
	//~ printf("S: %d\n", cp.func[PROBE_STATUS](cp.ctx));

	sleep(2);

	sleep(3);
	//~ if (cp.func[PROBE_TARGET_PAUSE](cp.ctx)) {
		//~ perror("Target pause error");
		//~ return 1;
	//~ }
	//~ puts("TPause");
	//~ printf("S: %d\n", cp.func[PROBE_STATUS](cp.ctx));

	sleep(3);
	//~ if (cp.func[PROBE_TARGET_RESUME](cp.ctx)) {
		//~ perror("Target resume error");
		//~ return 1;
	//~ }
	//~ puts("TResume");
	//~ printf("S: %d\n", cp.func[PROBE_STATUS](cp.ctx));

	sleep(3);

	puts("--");
	if (cp.func[PROBE_STOP](cp.ctx)) {
		perror("Probe stop error");
		return 1;
	}
	printf("S: %d\n", cp.func[PROBE_STATUS](cp.ctx));

	pthread_join(logthread, NULL);

	if (cp.func[PROBE_DESTROY](cp.ctx)) {
		perror("Destroy error");
		return 1;
	}
	free(pout.data);
	pthread_mutex_destroy(&pout.update_mutex);
	pthread_cond_destroy(&pout.update_cond);

	return 0;
}
