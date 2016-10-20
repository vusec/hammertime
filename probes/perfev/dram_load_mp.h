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

/* Multi-process version of dram_load */

#ifndef _PROBE_DRAM_LOAD_MP_H
#define _PROBE_DRAM_LOAD_MP_H 1

//~ #define PEPROBE_MP_DEBUG

#include "dram_load.h"

/* Setup a probe attached to the entire system */
int probe_dramload_setup_sys(struct ProbeOutput *pout, struct ProbeControlPanel *pcp);

#endif /* dram_load_mp.h */
