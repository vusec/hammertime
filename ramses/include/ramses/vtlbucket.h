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
 * Interface used by VTLB storage data structures, a.k.a. "buckets"
 * A bucket is a key-value mapping that ideally supports fast lookup.
 */

#ifndef _HAMTIME_RAMSES_VTLBUCKET_H
#define _HAMTIME_RAMSES_VTLBUCKET_H 1

#include <stdint.h>

struct VTLBucketFuncs {
	/* Bucket functions */
	/*
	 * Search for key. Return 0 if present, nonzero otherwise.
	 * handle, if not NULL, will be set to an appropriate value for use with
	 * get and/or insert.
	 */
	int (*search)(void *self, uint64_t key, int64_t *handle);
	/* Return value stored at handle */
	uint64_t (*get)(void *self, int64_t handle);
	/* Insert/replace value at handle */
	void (*insert)(void *self, uint64_t key, uint64_t val, int64_t handle);
	/* Clear the entire data structure */
	void (*clear)(void *self);
};

/* Convenience inline functions; call these from your code */

static inline int
ramses_vtlbucket_search(struct VTLBucketFuncs *b, void *self,
                        uint64_t key, int64_t *handle)
{
	return b->search(self, key, handle);
}

static inline uint64_t
ramses_vtlbucket_get(struct VTLBucketFuncs *b, void *self, int64_t handle)
{
	return b->get(self, handle);
}

static inline void
ramses_vtlbucket_insert(struct VTLBucketFuncs *b, void *self,
                        uint64_t key, uint64_t val, int64_t handle)
{
	b->insert(self, key, val, handle);
}

static inline void ramses_vtlbucket_clear(struct VTLBucketFuncs *b, void *self)
{
	b->clear(self);
}

#endif /* vtlbucket.h */
