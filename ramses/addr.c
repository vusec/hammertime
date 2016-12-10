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

#include "bitops.h"

#include <ramses/addr.h>

#include <stddef.h>

/* Physical Address Routing */
/* PAROUTE_PASSTHRU */
static memaddr_t noop_route(physaddr_t addr, const struct SysMemMapOpts *opts)
{
	return (memaddr_t)addr;
}

static physaddr_t noop_route_reverse(memaddr_t addr, const struct SysMemMapOpts *opts)
{
	return (physaddr_t)addr;
}

/* PAROUTE_X86_GENERIC */
static memaddr_t x86_route(physaddr_t addr, const struct SysMemMapOpts *opts)
{
	if (opts != NULL &&
	    (opts->flags & SMM_FLAG_X86_REMAP) &&
	    opts->argsize >= SMM_ARGSIZE_X86_GENERIC)
	{
		physaddr_t pcistart = opts->args[SMM_ARG_X86_PCISTART];
		physaddr_t tom = opts->args[SMM_ARG_X86_TOPOFMEM];
		if (opts->flags & SMM_FLAG_X86_INTEL_ME) {
			tom -= 16 * (1 << 20);
		}

		return (addr < tom) ? addr : (pcistart + (addr - tom));
	} else {
		return noop_route(addr, opts);
	}
}

static physaddr_t x86_route_reverse(memaddr_t addr, const struct SysMemMapOpts *opts)
{
	if (opts != NULL &&
	    (opts->flags & SMM_FLAG_X86_REMAP) &&
	    opts->argsize >= SMM_ARGSIZE_X86_GENERIC)
	{
		physaddr_t pcistart = opts->args[SMM_ARG_X86_PCISTART];
		physaddr_t tom = opts->args[SMM_ARG_X86_TOPOFMEM];
		if (opts->flags & SMM_FLAG_X86_INTEL_ME) {
			tom -= 16 * (1 << 20);
		}

		return ((addr > pcistart && addr < (4 * (1L << 30))) ? (addr - pcistart + tom) : addr);
	} else {
		return noop_route_reverse(addr, opts);
	}
}

/* Function tables and exports */
memaddr_t (*const routing_funcs[])(physaddr_t addr, const struct SysMemMapOpts *opts) = {
	[PAROUTE_PASSTHRU] = noop_route,
	[PAROUTE_X86_GENERIC] = x86_route,
};
physaddr_t (*const routing_reverse_funcs[])(memaddr_t addr, const struct SysMemMapOpts *opts) = {
	[PAROUTE_PASSTHRU] = noop_route_reverse,
	[PAROUTE_X86_GENERIC] = x86_route_reverse,
};

memaddr_t ramses_route(enum PhysAddrRouter r, physaddr_t addr, const struct SysMemMapOpts *opts)
{
	return routing_funcs[r](addr, opts);
}
physaddr_t ramses_route_reverse(enum PhysAddrRouter r, memaddr_t addr, const struct SysMemMapOpts *opts)
{
	return routing_reverse_funcs[r](addr, opts);
}

/* DRAM Address mapping */

static struct DRAMAddr ddr3_rank_mirror(struct DRAMAddr addr)
{
	struct DRAMAddr ret = addr;
	/* Switch address bits 3<->4 5<->6 7<->8 */
	ret.row &= 0xfe07;
	ret.row |= (BIT(7, addr.row) << 8) | (BIT(8, addr.row) << 7) |
	           (BIT(5, addr.row) << 6) | (BIT(6, addr.row) << 5) |
	           (BIT(3, addr.row) << 4) | (BIT(4, addr.row) << 3);
	ret.col &= 0xfe07;
	ret.col |= (BIT(7, addr.col) << 8) | (BIT(8, addr.col) << 7) |
	           (BIT(5, addr.col) << 6) | (BIT(6, addr.col) << 5) |
	           (BIT(3, addr.col) << 4) | (BIT(4, addr.col) << 3);
	/* Switch bank bits 0<->1 */
	ret.bank &= 0xfffc;
	ret.bank |= (BIT(0, addr.bank) << 1) | BIT(1, addr.bank);

	return ret;
}

/* MEMCTRL_NAIVE_DDR3 */
static struct DRAMAddr map_naive_ddr3(memaddr_t addr, int geom_flags, const void *opts)
{
	struct DRAMAddr retval = {
		.chan = 0,
		.dimm = 0,
		.rank = 0,
		.col = (addr >> 3) & LS_BITMASK(10),
		.bank = (addr >> 13) & LS_BITMASK(3),
		.row = (addr >> 16),
	};
	return retval;
}

static memaddr_t map_reverse_naive_ddr3(struct DRAMAddr addr, int geom_flags, const void *opts)
{
	return (addr.row << 16) + (addr.bank << 13) + (addr.col << 3);
}

/* MEMCTRL_NAIVE_DDR4 */
static struct DRAMAddr map_naive_ddr4(memaddr_t addr, int geom_flags, const void *opts)
{
	struct DRAMAddr retval = {
		.chan = 0,
		.dimm = 0,
		.rank = 0,
		.col = (addr >> 3) & LS_BITMASK(10),
		.bank = (addr >> 13) & LS_BITMASK(4),
		.row = (addr >> 17),
	};
	return retval;
}

static memaddr_t map_reverse_naive_ddr4(struct DRAMAddr addr, int geom_flags, const void *opts)
{
	return (addr.row << 17) + (addr.bank << 13) + (addr.col << 3);
}

/* MEMCTRL_INTEL_SANDY_DDR3 */
static struct DRAMAddr map_sandy(memaddr_t addr, int geom_flags, const void *opts)
{
	struct DRAMAddr retval = {0,0,0,0,0,0};
	/* Idx: 0 */

	/* Discard index into memory word */
	addr >>= 3;
	/* Idx: 3 */
	if (geom_flags & MEMGEOM_CHANSELECT) {
		retval.col = addr & LS_BITMASK(3);
		addr >>= 3;
		retval.chan = BIT(0,addr);
		addr >>= 1;
		retval.col += (addr & LS_BITMASK(7)) << 3;
		addr >>= 7;
	} else {
		retval.col = addr & LS_BITMASK(10);
		addr >>= 10;
	}
	/* Idx: 13/14 */
	/* HACK: DIMM selection rule assumed (and you know what they say about when you assume) */
	if (geom_flags & MEMGEOM_DIMMSELECT) {
		retval.dimm += BIT(3,addr);
		addr = POP_BIT(3,addr);
	}
	/* Idx: 13/14, Possible Holes: 16/17 */
	if (geom_flags & MEMGEOM_RANKSELECT) {
		retval.rank += BIT(3,addr);
		addr = POP_BIT(3,addr);
	}
	/* Idx: 13/14, Possible Holes: 16/17, 17/18 */
	for (int i = 0; i < 3; i++) {
		retval.bank += (BIT(0,addr) ^ BIT(3,addr)) << i;
		addr >>= 1;
	}
	retval.row = addr & LS_BITMASK(16);
	addr >>= 16;

	if (opts != NULL) {
		const struct IntelCntrlOpts *o = (const struct IntelCntrlOpts *)opts;
		if (o->flags | MEMCTRLOPT_INTEL_RANKMIRROR && BIT(0, retval.rank)) {
			retval = ddr3_rank_mirror(retval);
		}
	}

	/* Sanity check that address "fits" in memory geometry */
	//~ assert(addr == 0);
	return retval;
}

static memaddr_t map_reverse_sandy(struct DRAMAddr addr, int geom_flags, const void *opts)
{
	memaddr_t retval = 0;

	if (opts != NULL) {
		const struct IntelCntrlOpts *o = (const struct IntelCntrlOpts *)opts;
		if (o->flags | MEMCTRLOPT_INTEL_RANKMIRROR && BIT(0, addr.rank)) {
			addr = ddr3_rank_mirror(addr);
		}
	}

	retval |= addr.row & LS_BITMASK(16);
	if (geom_flags & MEMGEOM_RANKSELECT) {
		retval <<= 1;
		retval |= addr.rank & 1;
	}
	if (geom_flags & MEMGEOM_DIMMSELECT) {
		retval <<= 1;
		retval |= addr.dimm & 1;
	}
	for (int i = 2; i >= 0; i--) {
		retval <<= 1;
		retval |= BIT(i, addr.bank) ^ BIT(i, addr.row);
	}
	if (geom_flags & MEMGEOM_CHANSELECT) {
		retval <<= 7;
		retval |= (addr.col >> 3) & LS_BITMASK(7);
		retval <<= 1;
		retval |= addr.chan & 1;
		retval <<= 3;
		retval |= addr.col & LS_BITMASK(3);
	} else {
		retval <<= 10;
		retval |= addr.col & LS_BITMASK(10);
	}
	retval <<= 3;

	return retval;
}

/* MEMCTRL_INTEL_IVYHASWELL_DDR3 */
static struct DRAMAddr map_ivyhaswell(memaddr_t addr, int geom_flags, const void *opts)
{
	struct DRAMAddr retval = {0,0,0,0,0,0};
	/* Idx: 0 */

	/* Discard index into memory word */
	addr >>= 3;
	/* Idx: 3 */
	if (geom_flags & MEMGEOM_CHANSELECT) {
		retval.col = addr & LS_BITMASK(4);
		addr >>= 4;
		retval.chan = BIT(0,addr) ^ BIT(1,addr) ^ BIT(2,addr) ^ BIT(5,addr) ^
					  BIT(6,addr) ^ BIT(11,addr) ^ BIT(12,addr);
		addr >>= 1;
		retval.col += (addr & LS_BITMASK(6)) << 4;
		addr >>= 6;
	} else {
		retval.col = addr & LS_BITMASK(10);
		addr >>= 10;
	}
	/* Idx: 13/14 */
	if (geom_flags & MEMGEOM_DIMMSELECT) {
		retval.dimm += BIT(2,addr);
		addr = POP_BIT(2,addr);
	}
	/* Idx: 13/14, Possible Holes: 15/16 */
	if (geom_flags & MEMGEOM_RANKSELECT) {
		retval.rank += BIT(2,addr) ^ BIT(6,addr);
		addr = POP_BIT(2,addr);
	}
	/* Idx: 13/14, Possible Holes: 15/16, 16/17 */
	for (int i = 0; i < 2; i++) {
		retval.bank += (BIT(0,addr) ^ BIT(3,addr)) << i;
		addr >>= 1;
	}
	retval.bank += (BIT(0,addr) ^ BIT((geom_flags & MEMGEOM_RANKSELECT) ? 4 : 3, addr)) << 2;
	addr >>= 1;

	retval.row = addr & LS_BITMASK(16);
	addr >>= 16;

	if (opts != NULL) {
		const struct IntelCntrlOpts *o = (const struct IntelCntrlOpts *)opts;
		if (o->flags | MEMCTRLOPT_INTEL_RANKMIRROR && BIT(0, retval.rank)) {
			retval = ddr3_rank_mirror(retval);
		}
	}

	/* Sanity check that address "fits" in memory geometry */
	//~ assert(addr == 0);
	return retval;
}

static memaddr_t map_reverse_ivyhaswell(struct DRAMAddr addr, int geom_flags, const void *opts)
{
	memaddr_t retval = 0;

	if (opts != NULL) {
		const struct IntelCntrlOpts *o = (const struct IntelCntrlOpts *)opts;
		if (o->flags | MEMCTRLOPT_INTEL_RANKMIRROR && BIT(0, addr.rank)) {
			addr = ddr3_rank_mirror(addr);
		}
	}

	retval |= addr.row & LS_BITMASK(16);
	if (geom_flags & MEMGEOM_RANKSELECT) {
		retval <<= 1;
		retval |= BIT(2, addr.bank) ^ BIT(3, addr.row);
		retval <<= 1;
		retval |= (addr.rank & 1) ^ BIT(2, addr.row);
	} else {
		retval <<= 1;
		retval |= BIT(2, addr.bank) ^ BIT(2, addr.row);
	}
	if (geom_flags & MEMGEOM_DIMMSELECT) {
		retval <<= 1;
		retval |= addr.dimm & 1;
	}
	for (int i = 1; i >= 0; i--) {
		retval <<= 1;
		retval |= BIT(i, addr.bank) ^ BIT(i, addr.row);
	}
	if (geom_flags & MEMGEOM_CHANSELECT) {
		retval <<= 6;
		retval |= (addr.col >> 4) & LS_BITMASK(6);
		retval <<= 1;
		retval |= (addr.chan & 1) ^ BIT(1,retval) ^ BIT(2,retval) ^
		          BIT(5,retval) ^ BIT(6,retval) ^ BIT(11,retval) ^ BIT(12,retval);
		retval <<= 4;
		retval |= addr.col & LS_BITMASK(4);
	} else {
		retval <<= 10;
		retval |= addr.col & LS_BITMASK(10);
	}
	retval <<= 3;

	return retval;
}

/* Function tables and exports */
struct DRAMAddr (*const map_funcs[])(memaddr_t addr, int geom_flags, const void *opts) = {
	[MEMCTRL_NAIVE_DDR3] = map_naive_ddr3,
	[MEMCTRL_NAIVE_DDR4] = map_naive_ddr4,
	[MEMCTRL_INTEL_SANDY_DDR3] = map_sandy,
	[MEMCTRL_INTEL_IVYHASWELL_DDR3] = map_ivyhaswell,
	//~ [MEMCTRL_INTEL_SKYLAKE_DDR4] = NULL,
};
memaddr_t (*const map_reverse_funcs[])(struct DRAMAddr addr, int geom_flags, const void *opts) = {
	[MEMCTRL_NAIVE_DDR3] = map_reverse_naive_ddr3,
	[MEMCTRL_NAIVE_DDR4] = map_reverse_naive_ddr4,
	[MEMCTRL_INTEL_SANDY_DDR3] = map_reverse_sandy,
	[MEMCTRL_INTEL_IVYHASWELL_DDR3] = map_reverse_ivyhaswell,
	//~ [MEMCTRL_INTEL_SKYLAKE_DDR4] = NULL,
};

struct DRAMAddr ramses_map(enum MemController c, memaddr_t addr, int geom_flags, const void *opts)
{
	return map_funcs[c](addr, geom_flags, opts);
}
memaddr_t ramses_map_reverse(enum MemController c, struct DRAMAddr addr, int geom_flags, const void *opts)
{
	return map_reverse_funcs[c](addr, geom_flags, opts);
}

memaddr_t ramses_map_granularity(enum MemController c, int geom_flags, const void *opts)
{
	const struct IntelCntrlOpts *o = (const struct IntelCntrlOpts *)opts;
	switch (c) {
		case MEMCTRL_NAIVE_DDR3:
		case MEMCTRL_NAIVE_DDR4:
			return (1 << 13);
		case MEMCTRL_INTEL_SANDY_DDR3:
			if (o != NULL && o->flags & MEMCTRLOPT_INTEL_RANKMIRROR)
				return (1 << 6);
			else
				return (geom_flags & MEMGEOM_CHANSELECT) ? (1 << 6) : (1 << 13);
		case MEMCTRL_INTEL_IVYHASWELL_DDR3:
			if (o != NULL && o->flags & MEMCTRLOPT_INTEL_RANKMIRROR)
				return (1 << 6);
			else
				return (geom_flags & MEMGEOM_CHANSELECT) ? (1 << 7) : (1 << 13);
		default:
			return 0x1000; /* We don't know any better; return a common page size */
	}
}

memaddr_t ramses_max_memory(enum MemController c, int geom_flags, const void *opts)
{
	int membits = 16 + 10 + 3; /* Rowbits + Colbits + Memory word index bits */
	switch (c) {
		case MEMCTRL_NAIVE_DDR3:
		case MEMCTRL_INTEL_SANDY_DDR3:
		case MEMCTRL_INTEL_IVYHASWELL_DDR3:
			membits += 3; /* 3 additional bits for bank selection */
			break;
		case MEMCTRL_NAIVE_DDR4:
			membits += 4; /* 4 bank bits in DDR4 */
			break;
		default:
			return 0; /* Unrecognized controller */
	}
	for (int g = geom_flags; g; g >>= 1) {
		membits += g & 1;
	}
	return (1UL << membits);
}


/* On-DIMM Address remapping */
/* DIMM_REMAP_NONE */
static struct DRAMAddr noop_remap(struct DRAMAddr addr) {return addr;}

/* DIMM_REMAP_R3X0 */
static struct DRAMAddr r3x0_remap(struct DRAMAddr addr)
{
	struct DRAMAddr retval = addr;
	retval.row ^= BIT(3, retval.row);
	return retval;
}
/* DIMM_REMAP_R3X21 */
static struct DRAMAddr r3x21_remap(struct DRAMAddr addr)
{
	struct DRAMAddr retval = addr;
	retval.row ^= BIT(3, retval.row) ? 6 : 0;
	return retval;
}
/* DIMM_REMAP_R3X210 */
static struct DRAMAddr r3x210_remap(struct DRAMAddr addr)
{
	struct DRAMAddr retval = addr;
	retval.row ^= BIT(3, retval.row) ? 7 : 0;
	return retval;
}

/* Function tables and exports */

struct DRAMAddr (*const remap_funcs[])(struct DRAMAddr addr) = {
	[DIMM_REMAP_NONE] = noop_remap,
	[DIMM_REMAP_R3X0] = r3x0_remap,
	[DIMM_REMAP_R3X21] = r3x21_remap,
	[DIMM_REMAP_R3X210] = r3x210_remap,
};
struct DRAMAddr (*const remap_reverse_funcs[])(struct DRAMAddr addr) = {
	[DIMM_REMAP_NONE] = noop_remap,
	[DIMM_REMAP_R3X0] = r3x0_remap,
	[DIMM_REMAP_R3X21] = r3x21_remap,
	[DIMM_REMAP_R3X210] = r3x210_remap,
};

struct DRAMAddr ramses_remap(enum DIMMRemap r, struct DRAMAddr addr)
{
	return remap_funcs[r](addr);
}
struct DRAMAddr ramses_remap_reverse(enum DIMMRemap r, struct DRAMAddr addr)
{
	return remap_reverse_funcs[r](addr);
}
