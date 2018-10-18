/*
 * Copyright (c) 2017-2018 Vrije Universiteit Amsterdam
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
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

#ifdef __linux__ /* Linux-specific allocator */
#define _GNU_SOURCE

#include "alloc.h"

#include <assert.h>
#include <stdint.h>

#include <unistd.h>
#include <sys/mman.h>

void *alloc_hammerbuf(size_t sz, size_t align, int flags)
{
	if (!sz) {
		return NULL;
	}
	const size_t PAGE_SIZE = sysconf(_SC_PAGESIZE);
	char *buf = NULL;
	int huge_sz = (flags >> ALLOC_HUGE_SHIFT) & 0xff;

	if (align <= PAGE_SIZE) {
		align = 0;
	}
	int mmap_flags = MAP_PRIVATE | MAP_ANONYMOUS;
	size_t reqsz = align ? sz + align : sz;

	if (huge_sz) {
		mmap_flags |= MAP_HUGETLB;
		mmap_flags |= (huge_sz << MAP_HUGE_SHIFT);
	}

	buf = mmap(NULL, reqsz, PROT_READ|PROT_WRITE, mmap_flags, -1, 0);
	if (buf != MAP_FAILED) {
		if (align) {
			size_t err = (uintptr_t)buf % align;
			size_t left = err ? align - err : 0;
			size_t right = align - left;
			munmap(buf, left);
			buf += left;
			assert((uintptr_t)buf % align == 0);
			munmap(buf + sz, right);
		}
		if (flags & ALLOC_THP) {
			madvise(buf, sz, MADV_HUGEPAGE);
		}
		if (flags & ALLOC_NOLOCK) {
			for (size_t p = 0; p < sz; p += PAGE_SIZE) {
				(void)*(volatile char *)(buf + p);
			}
		} else {
			mlock(buf, sz);
		}
		return buf;
	} else {
		return NULL;
	}
}

int free_hammerbuf(void *buf, size_t sz)
{
	return munmap(buf, sz);
}

#elif defined(unix) || defined(__unix) || defined(__unix__) /* POSIX fallback */
#define _POSIX_C_SOURCE 200809

#include "alloc.h"

#include <stdlib.h>
#include <errno.h>

#include <unistd.h>
#include <sys/mman.h>

void *alloc_hammerbuf(size_t sz, size_t align, int flags)
{
	if (!sz) {
		return NULL;
	}
	const size_t PAGE_SIZE = sysconf(_SC_PAGESIZE);
	int huge_sz = (flags >> ALLOC_HUGE_SHIFT) & 0xff;
	void *buf = NULL;
	int r = 0;

	if (huge_sz || flags & ALLOC_THP) {
		errno = EINVAL;
		return NULL;
	}
	if (align < PAGE_SIZE) {
		align = PAGE_SIZE;
	}
	if ((r = posix_memalign(&buf, align, sz))) {
		errno = r;
		return NULL;
	}
	if (flags & ALLOC_NOLOCK) {
		for (size_t p = 0; p < sz; p += PAGE_SIZE) {
			(void)*(volatile char *)((char *)buf + p);
		}
	} else {
		mlock(buf, sz);
	}
	return buf;
}

int free_hammerbuf(void *buf, size_t sz)
{
	free(buf);
	return 0;
}

#endif /* OS selection */
