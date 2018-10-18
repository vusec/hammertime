/*
 * Copyright (c) 2017-2018 Vrije Universiteit Amsterdam
 *
 * This program is licensed under the GPL2+.
 */

#ifndef ALLOC_H
#define ALLOC_H 1

#include <stddef.h>

#define ALLOC_THP     	1 /* Advise system to use Transparent Huge Pages */
/*#define RESERVED	2 */
#define ALLOC_NOLOCK  	4 /* Do not lock buffer to RAM (NOT RECOMMENDED) */

#define ALLOC_HUGE_SHIFT 8

#ifdef __x86_64__
#define ALLOC_HUGE_2MB	(21 << ALLOC_HUGE_SHIFT) /* Use 2MB hugetlb pages */
#define ALLOC_HUGE_1GB	(30 << ALLOC_HUGE_SHIFT) /* Use 1GB hugetlb pages */
static const int HUGE_SHIFTS[] = { 0, ALLOC_HUGE_2MB, ALLOC_HUGE_1GB };
#else
static const int HUGE_SHIFTS[] = { 0 };
#endif
static const size_t HUGE_SHIFTS_LEN = sizeof(HUGE_SHIFTS) / sizeof(*HUGE_SHIFTS);

void *alloc_hammerbuf(size_t sz, size_t align, int flags);
int free_hammerbuf(void *buf, size_t sz);
#endif /* alloc.h */
