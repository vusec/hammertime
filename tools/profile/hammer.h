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

#ifndef _HAMMER_H
#define _HAMMER_H 1

void fill(void *buffer, size_t buflen, const void *pattern, size_t patlen);
size_t check(void *buffer, size_t buflen, const void *pattern, size_t patlen, size_t bufidx);

void hammer_single(const uintptr_t *addrs, unsigned int itercount);
void hammer_double(const uintptr_t *addrs, unsigned int itercount);
void hammer_triple(const uintptr_t *addrs, unsigned int itercount);
void hammer_quad(const uintptr_t *addrs, unsigned int itercount);
void hammer_six(const uintptr_t *addrs, unsigned int itercount);
unsigned int calibrate_hammer(void (*hammer_func)(const uintptr_t *, unsigned int), const uintptr_t *addrs,
                              unsigned int dram_refresh_period_us, unsigned int max_overshoot_us);

#endif /* hammer.h */
