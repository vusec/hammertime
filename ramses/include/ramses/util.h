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

/* Miscellaneous utility functions for DRAM addresses */

#ifndef _HAMTIME_RAMSES_UTIL_H
#define _HAMTIME_RAMSES_UTIL_H 1

#include <ramses/types.h>

#include <stdbool.h>

/* For use in printf() */
#define DRAMADDR_HEX_FMTSTR "(%1x %1x %1x %1x %4x %3x)"


static inline int ramses_dramaddr_cmp(struct DRAMAddr a, struct DRAMAddr b)
{
	int64_t amag = ((int64_t)a.chan << 56) + ((int64_t)a.dimm << 48) + ((int64_t)a.rank << 40) +
	               ((int64_t)a.bank << 32) + ((int64_t)a.row << 16) + a.col;
	int64_t bmag = ((int64_t)b.chan << 56) + ((int64_t)b.dimm << 48) + ((int64_t)b.rank << 40) +
	               ((int64_t)b.bank << 32) + ((int64_t)b.row << 16) + b.col;
	int64_t d = amag - bmag;
	return d ? ((d > 0) ? 1 : -1) : 0;
}

static inline bool ramses_same_bank(struct DRAMAddr a, struct DRAMAddr b)
{
	return (
		a.chan == b.chan &&
		a.dimm == b.dimm &&
		a.rank == b.rank &&
		a.bank == b.bank
	);
}

static inline bool ramses_same_row(struct DRAMAddr a, struct DRAMAddr b)
{
	return (ramses_same_bank(a,b) && a.row == b.row);
}

static inline bool ramses_succ_rows(struct DRAMAddr a, struct DRAMAddr b)
{
	return (ramses_same_bank(a,b) && (a.row + 1) == b.row);
}

static inline struct DRAMAddr ramses_dramaddr_diff(struct DRAMAddr a, struct DRAMAddr b)
{
	struct DRAMAddr ret = {
		.chan = a.chan - b.chan,
		.dimm = a.dimm - b.dimm,
		.rank = a.rank - b.rank,
		.bank = a.bank - b.bank,
		.row = a.row - b.row,
		.col = a.col - b.col
	};
	return ret;
}

static inline struct DRAMAddr ramses_dramaddr_add(struct DRAMAddr a, struct DRAMAddr b)
{
	struct DRAMAddr ret = {
		.chan = a.chan + b.chan,
		.dimm = a.dimm + b.dimm,
		.rank = a.rank + b.rank,
		.bank = a.bank + b.bank,
		.row = a.row + b.row,
		.col = a.col + b.col
	};
	return ret;
}

static inline int ramses_dramaddr_rowdiff(struct DRAMAddr a, struct DRAMAddr b)
{
	return ramses_same_bank(a,b) ? (a.row - b.row) : 0xfffff;
}

static inline struct DRAMAddr ramses_dramaddr_addrows(struct DRAMAddr a, int rd)
{
	struct DRAMAddr ret = {
		.chan = a.chan,
		.dimm = a.dimm,
		.rank = a.rank,
		.bank = a.bank,
		.row = a.row + rd,
		.col = a.col
	};
	return ret;
}

#endif /* util.h */
