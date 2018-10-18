/*
 * Copyright (c) 2017-2018 Vrije Universiteit Amsterdam
 *
 * This program is licensed under the GPL2+.
 */

#ifndef HAMMER_H
#define HAMMER_H 1

#include <stdint.h>

typedef void (*hammerfunc_t)(uintptr_t, uintptr_t, unsigned long, long);

void hammer_ld(uintptr_t a, uintptr_t b, unsigned long iters, long opt);

#if (__i386__ || __i386 || __x86_64 || __x86_64__)
#define HAMMER_X86 1
void hammer_ld_flush(uintptr_t a, uintptr_t b, unsigned long iters, long opt);
void hammer_ld_flush_mfence(uintptr_t a, uintptr_t b, unsigned long iters, long opt);
#endif

#endif /* hammer.h */
