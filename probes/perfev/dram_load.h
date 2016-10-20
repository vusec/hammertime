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

/* Single-process probe that captures memory load operations missing the last
 * level of cache.
 */

#ifndef _PROBE_DRAM_LOAD_H
#define _PROBE_DRAM_LOAD_H 1

#include "../probe.h"

#define DRAMLOAD_ERR_MEMALLOC	1
#define DRAMLOAD_ERR_PAGEMAP	2
#define DRAMLOAD_ERR_VTLB		3
#define DRAMLOAD_ERR_PERFEV		4
#define DRAMLOAD_ERR_ATTACH		5
#define DRAMLOAD_ERR_POLLSTER	6
#define DRAMLOAD_ERR_POUT		7

int probe_dramload_setup_child(struct ProbeOutput *pout,
                               struct ProbeControlPanel *pcp,
                               char *execpath, char *argv[], char *envp[],
                               int multitask,
                               pid_t *cpid);
int probe_dramload_setup_pid(struct ProbeOutput *pout,
                             struct ProbeControlPanel *pcp,
                             pid_t target_pid,
                             int multitask);

#endif /* dram_load.h */
