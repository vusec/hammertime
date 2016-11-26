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

/*
 * VTLB -- Virtual Translation Lookahead Buffer
 * A VTLB is a data structure that allows efficient caching of
 * physical-to-virtual address mappings found in /proc/[pid]/pagemap.
 * Provides a simple lookup interface that handles cache hits and misses.
 *
 * It is no ordinary cache though; by providing hooks to keep track of time it
 * ensures that all satisfied requests are based on information no older than a
 * set limit.
 * Also, by using multiple generations of caches, it mostly smoothes out
 * the massive hitrate slumps associated with expiring cache contents.
 *
 * Uses VTLBuckets as generational caches.
 */

#ifndef _HAMTIME_RAMSES_VTLB_H
#define _HAMTIME_RAMSES_VTLB_H 1

#include <ramses/types.h>
#include <ramses/vtlbucket.h>

//~ #define RAMSES_VTLB_DEBUG

/*
 * Set up a VTLB with provided parameters.
 * Time values are in microseconds.
 * The buckets are set up automatically to use a sane default.
 * On success, returns an opaque pointer to be passed to other ramses_vtlb_* functions.
 * On failure, returns NULL and sets errno appropriately.
 */
void *ramses_vtlb_create(unsigned int gensize, unsigned int num_gen,
                         unsigned long min_trust_us, unsigned long max_trust_us,
                         int pagemap_fd);

/*
 * Set up a VTLB with custom buckets.
 * Return value is the same as above.
 */
void *ramses_vtlb_create_cust_buckets(struct VTLBucketFuncs *bfuncs,
                                      void *buckets[], unsigned int num_gen,
                                      unsigned long min_trust_us,
                                      unsigned long max_trust_us,
                                      int pagemap_fd);

/* Destroy a VTLB, freeing all held resources */
void ramses_vtlb_destroy(void *vtlb);

/*
 * Update the internal time state of the VTLB.
 * Time values are expressed in nanoseconds.
 * Choose either one of these functions to use for a particular VTLB instance.
 * Mixing and matching is possible, but will trigger a complete flush when
 * changing working regime from timedelta to timestamp or vice-versa.
 */
void ramses_vtlb_update_timedelta(void *vtlb, int64_t timev);
void ramses_vtlb_update_timestamp(void *vtlb, int64_t timev);

/* Searches the VTLB exclusively, with no fallback */
physaddr_t ramses_vtlb_search(void *vtlb, uint64_t vpfn);
/* Look up an virtual page frame number in the VTLB, falling back to pagemap */
physaddr_t ramses_vtlb_lookup(void *vtlb, uint64_t vpfn);
/* Updates (i.e. inserts or replaces) an entry in the most recent generation */
void ramses_vtlb_update(void *vtlb, uint64_t vpfn, physaddr_t pfn);
/* Flush the VTLB; do not update time state */
void ramses_vtlb_flush(void *vtlb);
/* Update the VTLB's /proc/[pid]/pagemap fd; automatically closes previous fd */
void ramses_vtlb_update_pagemapfd(void *vtlb, int pagemap_fd);

#ifdef RAMSES_VTLB_DEBUG
float ramses_vtlb_hitrate(void *vtlb);
double ramses_vtlb_avg_hit_time(void *vtlb);
double ramses_vtlb_avg_miss_time(void *vtlb);
float ramses_vtlb_avg_probe(void *vtlb);
uint64_t ramses_vtlb_get_nreqs(void *vtlb);
void ramses_vtlb_clear_stats(void *vtlb);
#endif

#endif /* vtlb.h */
