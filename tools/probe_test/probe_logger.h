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

/* Probe output consumers that logs the stream to a file */

#ifndef _HAMTIME_PROBE_LOGGER_H
#define _HAMTIME_PROBE_LOGGER_H 1

#include "probes/probe.h"

#include <stdio.h>

/* Log timig info */
int log_probe_timing(struct ProbeOutput *pout, FILE *f);

#endif /* name.h */
