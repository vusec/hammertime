/*
 * Copyright (c) 2017-2018 Vrije Universiteit Amsterdam
 *
 * This program is licensed under the GPL2+.
 */

#ifndef MEM_H
#define MEM_H 1

#include <stddef.h>

/* Fill memory with a repeating pattern */
void mem_fill(void *buffer, size_t buflen,
              const void *pattern, size_t patlen,
              int inverted);
/*
 * Check memory for a repeating pattern starting from bufidx.
 * Returns the index of the first non-matching byte found, or buflen if the
 * entire buffer matches.
 */
size_t mem_check(void *buffer, size_t buflen,
                 const void *pattern, size_t patlen,
                 size_t bufidx, int inverted);
/* Ensure the buffer is evicted from all levels of CPU cache */
void mem_flush(void *buffer, size_t buflen);

#endif /* mem.h */
