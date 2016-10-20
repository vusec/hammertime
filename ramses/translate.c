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

#include <ramses/translate.h>

#include <errno.h>
#include <unistd.h>

physaddr_t ramses_translate_heuristic(uintptr_t addr, int prop_bits, physaddr_t base)
{
	return ((addr & LS_BITMASK(prop_bits)) + base);
}

physaddr_t ramses_translate_pagemap(uintptr_t addr, int pagemap_fd)
{
	uint64_t pagemap_entry;
	off_t pagemap_off = (addr >> 12) * sizeof(pagemap_entry);
	if (pread(pagemap_fd, &pagemap_entry, sizeof(pagemap_entry), pagemap_off) != sizeof(pagemap_entry)) {
		return (physaddr_t)-1;
	}
	/* Sanity check that page is in memory */
	if (!BIT(63, pagemap_entry)) {
		errno = ENODATA;
		return (physaddr_t)-1;
	}
	return ((pagemap_entry & LS_BITMASK(55)) << 12) +
			(addr & LS_BITMASK(12));
}
