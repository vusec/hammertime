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

#include "memfiles.h"

#include <stdio.h>
#include <errno.h>
#include <stdint.h>

#include <fcntl.h>

#define PATH_MAX 20

int memfile_pidmem(pid_t pid, int flags)
{
	char path[PATH_MAX];
	if (snprintf(path, PATH_MAX, "/proc/%d/mem", pid) > PATH_MAX) {
		errno = EDOM;
		return -1;
	}
	return open(path, (flags & MEMFILE_WRITABLE) ? O_RDWR : O_RDONLY);
}

int memfile_devmem(int flags)
{
	return open("/dev/mem", (flags & MEMFILE_WRITABLE) ? O_RDWR|O_SYNC : O_RDONLY);
}

int memfile_flip_bits(int fd, off_t offset, uint8_t pullup, uint8_t pulldown)
{
	uint8_t buf, upbuf;
	if (pread(fd, &buf, sizeof(buf), offset) != sizeof(buf)) {
		return -1;
	}
	upbuf = buf | pullup;
	buf = upbuf & ~(pulldown & ~(buf ^ upbuf)); /* A little bit of magic here.
	                                             * Prevents us pulling down bits we just pulled up */
	if (pwrite(fd, &buf, sizeof(buf), offset) != sizeof(buf)) {
		return -1;
	}
	return 0;
}
