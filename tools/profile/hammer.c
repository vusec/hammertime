/*
 * Copyright (c) 2016 Andrei Tatar
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

#include <string.h>
#include <stdint.h>
#include <time.h>


void fill(void *buffer, size_t buflen, const void *pattern, size_t patlen)
{
	if (patlen == 1) {
		memset(buffer, ((char *)pattern)[0], buflen);
	} else {
		for (size_t i = 0; i < buflen; i++) {
			((char *)buffer)[i] = ((char *)pattern)[i % patlen];
		}
	}
}

size_t check(void *buffer, size_t buflen, const void *pattern, size_t patlen, size_t bufidx)
{
	if (patlen == 1) {
		char expected = ((char *)pattern)[0];
		for (; bufidx < buflen; bufidx++) {
			if (((char *)buffer)[bufidx] != expected) {
				break;
			}
		}
	} else {
		for (; bufidx < buflen; bufidx++) {
			if (((char *)buffer)[bufidx] != ((char *)pattern)[bufidx % patlen]) {
				break;
			}
		}
	}
	return bufidx;
}

void hammer_single(const uintptr_t *addrs, unsigned int itercount)
{
	volatile int *p = (volatile int *)(addrs[0]);

	for (; itercount --> 0;) {
		*p;
		asm volatile ("clflush (%0)\n\t" : : "r" (p) : "memory");
	}
}

void hammer_double(const uintptr_t *addrs, unsigned int itercount)
{
	volatile int *p = (volatile int *)(addrs[0]);
	volatile int *q = (volatile int *)(addrs[1]);

	for (; itercount --> 0;) {
		*p;
		*q;
		asm volatile (
			"clflush (%0)\n\t"
			"clflush (%1)\n\t"
			 :
			 : "r" (p), "r" (q)
			 : "memory"
		);
	}
}

void hammer_triple(const uintptr_t *addrs, unsigned int itercount)
{
	volatile int *p = (volatile int *)(addrs[0]);
	volatile int *q = (volatile int *)(addrs[1]);
	volatile int *r = (volatile int *)(addrs[2]);

	for (; itercount --> 0;) {
		*p;
		*q;
		*r;
		asm volatile (
			"clflush (%0)\n\t"
			"clflush (%1)\n\t"
			"clflush (%2)\n\t"
			 :
			 : "r" (p), "r" (q), "r" (r)
			 : "memory"
		);
	}
}

void hammer_quad(const uintptr_t *addrs, unsigned int itercount)
{
	volatile int *p1 = (volatile int *)(addrs[0]);
	volatile int *q1 = (volatile int *)(addrs[1]);
	volatile int *p2 = (volatile int *)(addrs[2]);
	volatile int *q2 = (volatile int *)(addrs[3]);

	for (; itercount --> 0;) {
		*p1;
		*q1;
		*p2;
		*q2;
		asm volatile (
			"clflush (%0)\n\t"
			"clflush (%1)\n\t"
			"clflush (%2)\n\t"
			"clflush (%3)\n\t"
			 :
			 : "r" (p1), "r" (q1), "r" (p2), "r" (q2)
			 : "memory"
		);
	}
}

void hammer_six(const uintptr_t *addrs, unsigned int itercount)
{
	volatile int *p1 = (volatile int *)(addrs[0]);
	volatile int *q1 = (volatile int *)(addrs[1]);
	volatile int *r1 = (volatile int *)(addrs[2]);
	volatile int *p2 = (volatile int *)(addrs[3]);
	volatile int *q2 = (volatile int *)(addrs[4]);
	volatile int *r2 = (volatile int *)(addrs[5]);

	for (; itercount --> 0;) {
		*p1;
		*q1;
		*r1;
		*p2;
		*q2;
		*r2;
		asm volatile (
			"clflush (%0)\n\t"
			"clflush (%1)\n\t"
			"clflush (%2)\n\t"
			"clflush (%3)\n\t"
			"clflush (%4)\n\t"
			"clflush (%5)\n\t"
			 :
			 : "r" (p1), "r" (q1), "r" (r1), "r" (p2), "r" (q2), "r" (r2)
			 : "memory"
		);
	}
}

unsigned int calibrate_hammer(void (*hammer_func)(const uintptr_t *, unsigned int), const uintptr_t *addrs,
							  unsigned int dram_refresh_period_us, unsigned int max_overshoot_us)
{
	unsigned int retval = 0;
	unsigned int gran = 0x100000;
	struct timespec t0, t;

	while (gran) {
		clock_gettime(CLOCK_REALTIME, &t0);
		hammer_func(addrs, (retval + gran) * 2);
		clock_gettime(CLOCK_REALTIME, &t);

		long int diff_us = ((t.tv_sec - t0.tv_sec)*1000000 + (t.tv_nsec - t0.tv_nsec)/1000) / 2;
		if (diff_us < dram_refresh_period_us) {
			retval += gran;
			continue;
		} else if (diff_us < dram_refresh_period_us + max_overshoot_us) {
			retval += gran;
			break;
		}
		gran >>= 1;
	}

	return retval;
}
