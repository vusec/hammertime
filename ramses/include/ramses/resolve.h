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

/* End-to-end address resolution for an emulated memory system */

#ifndef _HAMTIME_RAMSES_RESOLVE_H
#define _HAMTIME_RAMSES_RESOLVE_H 1

#include <ramses/addr.h>

#include <stdio.h>

struct MemorySystem {
	enum PhysAddrRouter router;
	enum MemController controller;
	enum DIMMRemap dimm_remap;
	int mem_geometry;
	struct SysMemMapOpts *route_opts;
	void *controller_opts;
};

/* Sets up a basic x86 memory system with PCI hole remapping enabled */
int ramses_memsys_setup_x86(enum MemController ctrl, int geom_flags, void *ctrlopt,
                            memaddr_t ramsize, physaddr_t pcistart, int intelme,
                            enum DIMMRemap remap, struct MemorySystem *output);

/*
 * Load a memory system from stream f.
 * err, if not NULL, will be used to print detailed error messages to.
 * File format is the one output by tools/msys_detect.py
 */
int ramses_memsys_load_file(FILE *f, struct MemorySystem *output, FILE *err);

/*
 * Load a memory system from string str.
 * Otherwise identical to ramses_memsys_load_file
 */
int ramses_memsys_load_str(char *s, size_t slen, struct MemorySystem *output, FILE *err);

/* Frees any buffers potentially allocated by ramses_memsys_* functions. */
void ramses_memsys_free(struct MemorySystem *s);

struct DRAMAddr ramses_resolve(struct MemorySystem *s, physaddr_t addr);
physaddr_t ramses_resolve_reverse(struct MemorySystem *s, struct DRAMAddr addr);

#endif /* resolve.h */
