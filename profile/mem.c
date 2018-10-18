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

#include "mem.h"

#include <string.h>

void mem_fill(void *buffer, size_t buflen,
              const void *pattern, size_t patlen,
              int inverted)
{
	unsigned char *b = (unsigned char *)buffer;
	unsigned char *p = (unsigned char *)pattern;
	if (patlen == 1) {
		unsigned char c = p[0];
		memset(buffer, inverted ? ~c : c, buflen);
	} else {
		for (size_t i = 0; i < buflen; i++) {
			unsigned char c = p[i % patlen];
			b[i] = inverted ? ~c : c;
		}
	}
}

size_t mem_check(void *buffer, size_t buflen,
                 const void *pattern, size_t patlen,
                 size_t bufidx, int inverted)
{
	unsigned char *b = (unsigned char *)buffer;
	unsigned char *p = (unsigned char *)pattern;
	if (patlen == 1) {
		unsigned char expected = inverted ? ~p[0] : p[0];
		for (; bufidx < buflen; bufidx++) {
			if (b[bufidx] != expected) {
				break;
			}
		}
	} else {
		for (; bufidx < buflen; bufidx++) {
			size_t patidx = bufidx % patlen;
			unsigned char expected = inverted ? ~p[patidx] : p[patidx];
			if (b[bufidx] != expected) {
				break;
			}
		}
	}
	return bufidx;
}


#if (__i386__ || __x86_64__)
void mem_flush(void *buffer, size_t buflen)
{
	const size_t CACHE_LINE_SIZE = 64;
	for (size_t i = 0; i < buflen; i += CACHE_LINE_SIZE) {
		__asm__ volatile ("clflush (%0)\n\t" : : "r" ((char *)buffer + i) : "memory");
	}
}
#endif
