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

/* Basic utility functions for using memory map files provided by Linux.
 * These include /proc/[pid]/mem and /dev/mem for virtual and physical memory,
 * respectively.
 */

#ifndef _HAMTIME_MEMFILES_H
#define _HAMTIME_MEMFILES_H 1

#include <stdint.h>

#include <unistd.h>

#define MEMFILE_WRITABLE	1

/* Get a file descriptor for pid's /proc/pid/mem file */
int memfile_pidmem(pid_t pid, int flags);
/* Get a file descriptor for /dev/mem; you may need a custom kernel for this */
int memfile_devmem(int flags);

/* Flip bits in one memory byte according to pull-up and pull-down flip masks.
 * Return 0 if successful, nonzero and sets errno otherwise
 */
int memfile_flip_bits(int fd, off_t offset, uint8_t pullup, uint8_t pulldown);

#endif /* memfiles.h */
