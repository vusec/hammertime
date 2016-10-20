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

#define _GNU_SOURCE

#include "addr.h"

#include <assert.h>
#include <unistd.h>
#include <stdlib.h>

#include <sys/types.h>
#include <fcntl.h>

#include <ramses/translate.h>
#include <ramses/util.h>


#define PAGE_SIZE 0x1000


static int physaddr_indexing_cmp_r(const void *a, const void *b, void *pfnarray)
{
	int64_t diff = ((physaddr_t *)pfnarray)[*(size_t *)a] - ((physaddr_t *)pfnarray)[*(size_t *)b];
	return diff ? ((diff > 0) ? 1 : -1) : 0;
}

static int geom_cmp(const void *a, const void *b)
{
	return ramses_dramaddr_cmp(((struct AddrEntry *)a)->dramaddr, ((struct AddrEntry *)b)->dramaddr);
}

size_t setup_targets(struct AddrEntry **targets, const void *targetbuf, size_t len,
					 struct MemorySystem *msys)
{
	int pagemap_fd;
#ifdef ADDR_DEBUG
	physaddr_t *pfns;
#endif
	physaddr_t *ramaddrs;
	size_t *pfn_order;
	size_t target_count;

	size_t pfncount = len / PAGE_SIZE;
	size_t granularity = ramses_map_granularity(msys->controller,
	                                            msys->mem_geometry,
	                                            msys->controller_opts);
	uintptr_t bufbase = (uintptr_t)targetbuf;

	pagemap_fd = open("/proc/self/pagemap", O_RDONLY);
	assert(pagemap_fd > 0);

#ifdef ADDR_DEBUG
	pfns = malloc(pfncount * sizeof(*pfns));
	assert(pfns != NULL);
#endif
	ramaddrs = malloc(pfncount * sizeof(*ramaddrs));
	assert(ramaddrs != NULL);
	pfn_order = malloc(pfncount * sizeof(*pfn_order));
	assert(pfn_order != NULL);

	for (size_t i = 0; i < pfncount; i++) {
		physaddr_t pa = ramses_translate_pagemap(bufbase + i * PAGE_SIZE, pagemap_fd);
#ifdef ADDR_DEBUG
		pfns[i] = pa;
#endif
		ramaddrs[i] = ramses_route(msys->router, pa, msys->route_opts);
		pfn_order[i] = i;
	}
	assert(close(pagemap_fd) == 0);

	qsort_r(pfn_order, pfncount, sizeof(*pfn_order), &physaddr_indexing_cmp_r, ramaddrs);

	if (granularity < PAGE_SIZE) {
		target_count = pfncount * (PAGE_SIZE / granularity);
		*targets = malloc(target_count * sizeof(**targets));
		assert(*targets != NULL);

		for (size_t i = 0, ti = 0; i < pfncount; i++) {
			physaddr_t curaddr = ramaddrs[pfn_order[i]];
			for (off_t off = 0; off < PAGE_SIZE; off += granularity) {
				(*targets)[ti].virtp = bufbase + pfn_order[i] * PAGE_SIZE + off;
				(*targets)[ti].len = granularity;
				(*targets)[ti].dramaddr = ramses_remap(
					msys->dimm_remap,
					ramses_map_addr(msys->controller, curaddr + off,
				                    msys->mem_geometry,msys->controller_opts)
				);
#ifdef ADDR_DEBUG
				(*targets)[ti].physaddr = pfns[pfn_order[i]] + off;
				(*targets)[ti].ramaddr = curaddr + off;
#endif
				ti++;
			}
		}
	} else {
		physaddr_t prev_ramaddr;
		target_count = pfncount;
		*targets = malloc(target_count * sizeof(**targets));
		assert(*targets != NULL);

		(*targets)[0].virtp = bufbase + pfn_order[0] * PAGE_SIZE;
		(*targets)[0].len = PAGE_SIZE;
		(*targets)[0].dramaddr = ramses_remap(
			msys->dimm_remap,
			ramses_map_addr(msys->controller, ramaddrs[pfn_order[0]],
		                    msys->mem_geometry, msys->controller_opts)
		);
		prev_ramaddr = ramaddrs[pfn_order[0]];
#ifdef ADDR_DEBUG
		(*targets)[0].physaddr = pfns[pfn_order[0]];
		(*targets)[0].ramaddr = ramaddrs[pfn_order[0]];
#endif
		for (size_t i = 1, ti = 1; i < pfncount; i++) {
			physaddr_t curaddr = ramaddrs[pfn_order[i]];
			size_t diff = curaddr - prev_ramaddr;
			assert(diff > 0);
			if (pfn_order[i-1] + 1 == pfn_order[i] && diff < granularity) {
				target_count--;
				(*targets)[ti-1].len += PAGE_SIZE;
				continue;
			} else {
				prev_ramaddr = curaddr;
				(*targets)[ti].virtp = bufbase + pfn_order[i] * PAGE_SIZE;
				(*targets)[ti].len = PAGE_SIZE;
				(*targets)[ti].dramaddr = ramses_remap(
					msys->dimm_remap,
					ramses_map_addr(msys->controller, curaddr,
				                    msys->mem_geometry, msys->controller_opts)
				);
#ifdef ADDR_DEBUG
				(*targets)[ti].physaddr = pfns[pfn_order[i]];
				(*targets)[ti].ramaddr = curaddr;
#endif
				ti++;
			}
		}

		*targets = realloc(*targets, target_count * sizeof(**targets));
	}
#ifdef ADDR_DEBUG
	free(pfns);
#endif
	free(ramaddrs);
	free(pfn_order);

	qsort(*targets, target_count, sizeof(**targets), &geom_cmp);
	return target_count;
}
