/*
 * Copyright (c) 2017-2018 Vrije Universiteit Amsterdam
 *
 * This program is licensed under the GPL2+.
 */

#include "hammer.h"

void hammer_ld(uintptr_t a, uintptr_t b, unsigned long iters, long opt)
{
	volatile int *p = (volatile int *)a;
	volatile int *q = (volatile int *)b;

	for (; iters --> 0;) { *p; *q; };
}

#ifdef HAMMER_X86

void hammer_ld_flush(uintptr_t a, uintptr_t b, unsigned long iters, long opt)
{
	volatile int *p = (volatile int *)a;
	volatile int *q = (volatile int *)b;

	for (; iters --> 0;) {
		__asm__ volatile (
			"clflush (%0)\n\t"
			"clflush (%1)\n\t"
			 :
			 : "r" (p), "r" (q)
			 : "memory"
		);
		*p;
		*q;
	}
}

void hammer_ld_flush_mfence(uintptr_t a, uintptr_t b,
                            unsigned long iters, long opt)
{
	volatile int *p = (volatile int *)a;
	volatile int *q = (volatile int *)b;

	for (; iters --> 0;) {
		__asm__ volatile (
			"clflush (%0)\n\t"
			"clflush (%1)\n\t"
			"mfence\n\t"
			 :
			 : "r" (p), "r" (q)
			 : "memory"
		);
		*p;
		*q;
	}
}
#endif
