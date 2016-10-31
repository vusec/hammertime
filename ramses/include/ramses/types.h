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

/* Types used by the RAMSES library */

#ifndef _HAMTIME_RAMSES_TYPES_H
#define _HAMTIME_RAMSES_TYPES_H 1

#include <stdint.h>

typedef uint64_t physaddr_t; /* Physical memory addresses; this is what the CPU sees */
typedef uint64_t memaddr_t; /* System memory addresses; this is what the memory controller sees */

/* Address reserved as error condition; nigh impossible to encounter in the wild */
#define RAMSES_BADADDR ((physaddr_t)-1)

struct DRAMAddr {
	uint8_t chan;
	uint8_t dimm;
	uint8_t rank;
	uint8_t bank;
	uint16_t row;
	uint16_t col;
}; /* DRAM Addresses; this is what the memory DIMMs see on the bus + selection pins */

#endif /* types.h */
