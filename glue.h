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

/* Glue code. Code that ties probes, predictors and everything together. */

#ifndef _HAMTIME_GLUE_H
#define _HAMTIME_GLUE_H 1

#include "probes/probe.h"
#include "predictors/predictor.h"

#include <ramses/resolve.h>

/* Main loop that uses a physical memory fd (e.g. /dev/mem) to induce bitflips
 *
 * NOTE: THIS DOES NOT CARE WHERE THE MEMORY IS MAPPED TO OR BY WHOM.
 * USING THIS WITH /dev/mem MAY LEAD TO DATA LOSS. YOU HAVE BEEN WARNED.
 */
void pmem_flip_loop(struct ProbeOutput *pout, struct Predictor *pred,
                    struct MemorySystem *msys, int pmemfd);

/* Main loop that uses a virtual memory file (/proc/pid/mem) to induce bitflips */
void vmem_flip_loop(struct ProbeOutput *pout, struct Predictor *pred,
                    struct MemorySystem *msys, pid_t pid);

#endif /* glue.h */
