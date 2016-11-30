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

/*
 * End-to-end memory system address handling.
 * This consists of 3 stages:
 *  	- Physical Address Routing
 *  	- DRAM Address Mapping
 *  	- On-DIMM DRAM Address Remapping
 */

#ifndef _HAMTIME_RAMSES_ADDR_H
#define _HAMTIME_RAMSES_ADDR_H 1

#include <ramses/types.h>

/* Physical Address Routing */

enum PhysAddrRouter {
	PAROUTE_PASSTHRU, /* No routing; memaddr = physaddr */

	PAROUTE_X86_GENERIC, /* Generic x86 memory routing */
};

struct SysMemMapOpts {
	uint32_t flags; /* Router-specific flags */
	uint32_t argsize; /* Length of the args[] array */
	physaddr_t args[]; /* Additional addresses that define the System Memory Map; router-specific */
};

#define SMM_FLAG_X86_REMAP		1 /* Enable x86 PCI memory hole remapping */
#define SMM_FLAG_X86_INTEL_ME	2 /* Intel Management Engine enabled (reserving top 16MB of RAM) */

/* argsize values for various routers */
#define SMM_ARGSIZE_PASSTHRU	0
#define SMM_ARGSIZE_X86_GENERIC	2

static inline uint32_t ramses_router_argsize(enum PhysAddrRouter r)
{
	switch(r) {
		case PAROUTE_PASSTHRU:
			return SMM_ARGSIZE_PASSTHRU;
		case PAROUTE_X86_GENERIC:
			return SMM_ARGSIZE_X86_GENERIC;
		default:
			return 0;
	}
}

/* args[] indices */
enum SmmArgsX86 {
	SMM_ARG_X86_PCISTART, /* Start of PCI MMIO region */
	SMM_ARG_X86_TOPOFMEM, /* Top of memory; e.g. 0x1000000000 for a system with 4GB RAM */
};

memaddr_t ramses_route(enum PhysAddrRouter r, physaddr_t addr, const struct SysMemMapOpts *opts);
physaddr_t ramses_route_reverse(enum PhysAddrRouter r, memaddr_t addr, const struct SysMemMapOpts *opts);


/* DRAM Address mapping */

enum MemController {
	MEMCTRL_NAIVE_DDR3, /* Simple column/bank/row slicing; DDR3 compatible -- 8 banks */
	MEMCTRL_NAIVE_DDR4, /* Simple column/bank/row slicing; DDR4 compatible -- 16 banks */

	MEMCTRL_INTEL_SANDY_DDR3,
	MEMCTRL_INTEL_IVYHASWELL_DDR3,
	//~ MEMCTRL_INTEL_SKYLAKE_DDR4,

	/* Convenience aliases */
	MEMCTRL_INTEL_IVY_DDR3 = MEMCTRL_INTEL_IVYHASWELL_DDR3,
	MEMCTRL_INTEL_HASWELL_DDR3 = MEMCTRL_INTEL_IVYHASWELL_DDR3,
};

#define MEMGEOM_RANKSELECT	1 /* 'Two ranks per dimm' */
#define MEMGEOM_DIMMSELECT	2 /* 'Two dimms per channel' */
#define MEMGEOM_CHANSELECT	4 /* 'Two channels per controller' */

struct IntelCntrlOpts {
	int flags;
};

#define MEMCTRLOPT_INTEL_RANKMIRROR	1 /* Enable address pin mirroring on the second rank */


struct DRAMAddr ramses_map(enum MemController c, memaddr_t addr, int geom_flags, const void *opts);
memaddr_t ramses_map_reverse(enum MemController c, struct DRAMAddr addr, int geom_flags, const void *opts);

memaddr_t ramses_map_granularity(enum MemController c, int geom_flags, const void *opts);
memaddr_t ramses_max_memory(enum MemController c, int geom_flags, const void *opts);


/* On-DIMM Address remapping */

enum DIMMRemap {
	DIMM_REMAP_NONE,
	DIMM_REMAP_R3X0, /* Bit 3 of the row address gets XORed into bit 0 */
	DIMM_REMAP_R3X21, /* Bit 3 of the row address gets XORed into bits 2 and 1 */
	DIMM_REMAP_R3X210,/* Bit 3 of the row address gests XORed into its 3 least significant bits */
};

struct DRAMAddr ramses_remap(enum DIMMRemap r, struct DRAMAddr addr);
struct DRAMAddr ramses_remap_reverse(enum DIMMRemap r, struct DRAMAddr addr);

#endif /* addr.h */
