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

/* Virtual Address Translation */

#ifndef _HAMTIME_RAMSES_TRANSLATE_H
#define _HAMTIME_RAMSES_TRANSLATE_H 1

#include <ramses/types.h>

/*
 * Heuristically attempt to translate a virtual address.
 * Keeps the prop_bits least significant bits of addr (assuming contiguousness) as a "page offset"
 * which it adds to base.
 *
 * Useful when you *know* you have a memory area that is physically contiguous
 * (e.g. 2MB pages => prop_bits = 21)
 */
physaddr_t ramses_translate_heuristic(uintptr_t addr, int prop_bits, physaddr_t base);

/*
 * Use the /proc/[pid]/pagemap interface to translate a virtual address.
 * Returns (physaddr_t)(-1) and sets errno if errors occur.
 * Sets errno to ENODATA if requested address is not mapped to memory.
 */
physaddr_t ramses_translate_pagemap(uintptr_t addr, int pagemap_fd);

#endif /* translate.h */
