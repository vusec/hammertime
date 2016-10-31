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

/* Simple demo program illustrating how artificial bitflips are generated
 * using the various parts of hammertime.
 *
 * As this project is still a work in progress, expect the function of this
 * program to change wildly, usually showcasing the latest and greatest stuff.
 */

#include "glue.h"
#include "memfiles.h"
#include "probes/perfev/dram_load.h"
#include "probes/perfev/dram_load_mp.h"
#include "predictors/fliptable.h"
#include "predictors/fliptbl_pred.h"

#include <ramses/resolve.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>

static const size_t BUFSIZE = 2 << 20;

/* Calibrated on dev machine.
 * Your mileage may vary; lower this if not getting bitflips.
 * Alternatively, raise this if flips are occurring all over the place.
 */
static const unsigned long HAMMER_THRESHOLD = 12500;

static const char *FLIPTBL_PATH = "fliptables/example.fliptbl";
static const char *MSYS_PATH = "fliptables/example.msys";

int main(int argc, char *argv[], char *envp[])
{
	int r;
	/* main_loop args */
	struct ProbeOutput pout;
	struct Predictor pred;
	struct MemorySystem msys;
	pid_t tpid;

	struct ProbeControlPanel cp;

	struct FlipTable ft;
	int ftfd = open(FLIPTBL_PATH, O_RDONLY);
	if (ftfd < 0) {
		perror("Flip table open error; errno");
		return 1;
	}
	if (fliptbl_load(ftfd, &ft)) {
		perror("Flip table load error; errno");
		return 1;
	}

	/* Init pred */
	if (init_fliptbl_predictor(&pred, &ft, HAMMER_DOUBLESIDED,
	                           HAMMER_THRESHOLD, EXTRAP_PERBANK_TRUNC)) {
		perror("Predictor init error; errno");
		return 1;
	}

	/* Init msys */
	//~ if (ramses_setup_x86_memsys(
	        //~ MEMCTRL_INTEL_HASWELL_DDR3,
	        //~ MEMGEOM_RANKSELECT,
	        //~ NULL,
	        //~ 8UL << 30,
	        //~ 0xbfaL << 20,
	        //~ 1,
	        //~ DIMM_REMAP_NONE,
	        //~ &msys)
	//~ ) {
		//~ perror("Memsys setup error; errno");
		//~ return 1;
	//~ }
	FILE *mfile = fopen(MSYS_PATH, "r");
	if (ramses_memsys_load_file(mfile, &msys, stderr)) {
		perror("Memsys setup error; errno");
		return 1;
	}
	fclose(mfile);

	/* Init pout */
	memset(&pout, 0, sizeof(pout));
	pout.fmtflags = PROBEOUT_VIRTADDR;
	pout.data = malloc(BUFSIZE);
	if (pout.data == NULL) {
		perror("POUT alloc error; errno");
		return 1;
	}
	pout.data_size = BUFSIZE;
	pthread_mutex_init(&pout.update_mutex, NULL);
	pthread_cond_init(&pout.update_cond, NULL);

	if (argc < 2) {
		printf("usage:\n %s <PID>\nOR\n %s -e <PROGRAM> [ARGS]\nOR\n %s -s\n", argv[0], argv[0], argv[0]);
		return 1;
	}
	if (strcmp(argv[1], "-e") == 0) {
		r = probe_dramload_setup_child(&pout, &cp, argv[2], &argv[2], envp, 1, &tpid);
	} else if (strcmp(argv[1], "-s") == 0) {
		r = probe_dramload_setup_sys(&pout, &cp);
		tpid = 0;
	} else {
		tpid = atoi(argv[1]);
		r = probe_dramload_setup_pid(&pout, &cp, tpid, 0);
	}
	if (r) {
		fprintf(stderr, "ecode: %d\n", r);
		perror("Probe setup error; errno");
		return 1;
	} else {
		printf("Attached; PID: %d\n", tpid);
	}

	if (cp.func[PROBE_START](cp.ctx)) {
		perror("Probe start error; errno");
		return 1;
	}
	r = cp.func[PROBE_TARGET_START](cp.ctx);
	if (r && r != PROBE_CPFUNC_NOTIMPL) {
		perror("Target start error; errno");
		return 1;
	}
	if (cp.func[PROBE_RESUME](cp.ctx)) {
		perror("Probe resume error; errno");
		return 1;
	}
	puts("Started");

	if (strcmp(argv[1], "-s") == 0) {
		int pmemfd = memfile_devmem(MEMFILE_WRITABLE);
		if (pmemfd < 0) {
			perror("Error opening /dev/mem");
			return 1;
		}
		puts("WARNING: RUNNING IN SYSTEM (/dev/mem) MODE");
		pmem_flip_loop(&pout, &pred, &msys, pmemfd);
	} else {
		vmem_flip_loop(&pout, &pred, &msys, tpid);
	}

	return 0;
}
