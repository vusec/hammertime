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

/* Fliptables --- efficient, binary lookup tables for rowhammer attacks
 *
 * A fliptable expresses the bitflips that occur on a particular DRAM chip when
 * "neighbour" rows an arbitrary distance away are being hammered.
 *
 * Fliptables are intended to be generated from rowhammer profiling runs.
 * Indeed, *any* valid `tools/profile' output can be losslessly expressed as one
 * or more fliptables. The reverse process -- losslessly going from binary
 * fliptables to human-readable profile output -- is also guaranteed.
 *
 * The particular details and conditions of the hammering are not recorded in
 * the file itself; use optional metadata files for that.
 */

#ifndef _HAMTIME_PRED_FLIPTABLE_H
#define _HAMTIME_PRED_FLIPTABLE_H 1

#include <ramses/types.h>

struct Flip {
	struct DRAMAddr location;
	uint16_t cell_byte;
	uint8_t pullup;
	uint8_t pulldown;
};

struct Hammering {
	uint32_t num_flips;
	uint32_t flip_idx;
};

struct Range {
	struct DRAMAddr start;
	uint32_t num_hammers;
	uint32_t ham_idx;
};

struct FlipTable {
	uint32_t dist;
	uint32_t num_ranges;
	struct Hammering *hammer_tbl;
	struct Flip *flip_tbl;
	struct Range *range_tbl;
	void *mmap;
};

enum ExtrapMode {
	EXTRAP_NONE,	/* Strict lookup; unknown addresses generate no bitflips */
	EXTRAP_PERBANK,	/* Per-bank extrapolation: an unknown address is aliased into
	               	 * an existing range located on the same bank */
	EXTRAP_PERBANK_TRUNC, /* Same as above, but with range lengths truncated to
	                       * the nearest power of 2 */
	EXTRAP_PERBANK_FIT, /* Attempt to "fit" ranges into a power of 2 sized virtual
	                     * range aligned at its own size used for extrapolation.
	                     * Requests falling outside this virtual range generate
	                     * no bitblips. */
};

/* Look up a rowhammering pattern targeted at addr in the fliptable ft.
 * If addr is not found bitflips are reported according to extrap.
 * Returns the number of bitflips and sets *flips to an array containing details.
 * If extrapolation is used, extrap_diff gets set to an offset with which flip
 * addresses must be corrected.
 * NOTE: DO NOT attempt to free() the flips array
 */
uint32_t fliptbl_lookup(struct FlipTable *ft, struct DRAMAddr addr,
                        enum ExtrapMode extrap, struct Flip **flips,
                        struct DRAMAddr *extrap_diff);

#define FLIPTBL_ERR_FILE	1 /* Error on file I/O */
#define FLIPTBL_ERR_MAGIC	2 /* Wrong magic number for fliptable */
#define FLIPTBL_ERR_MMAP	3 /* Error mmap()-ing the fliptable */

/* Load a fliptable file from fd and populate ft */
int fliptbl_load(int fd, struct FlipTable *ft);
/* Unload the fliptable file from memory. Does not also free ft. */
int fliptbl_close(struct FlipTable *ft);

#endif /* fliptable.h */
