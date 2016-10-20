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

#include <ramses/vtlb_hashtbl.h>

#include "bitops.h"

#include <stdlib.h>
#include <string.h>

struct htbl_entry {
	uint64_t va;
	uint64_t pa;
};

struct htbl_state {
	unsigned int size;
	int probe_limit;
	unsigned int (*hashfunc)(uint64_t key, unsigned int sz);
	struct htbl_entry buf[];
};

unsigned int ramses_hash_trivial(uint64_t key, unsigned int sz)
{
	return (unsigned int)(key % sz);
}

unsigned int ramses_hash_twang6432(uint64_t key, unsigned int sz)
{
	key = (~key) + (key << 18); // key = (key << 18) - key - 1;
	key ^= key >> 31;
	key *= 21; // key = (key + (key << 2)) + (key << 4);
	key ^= key >> 11;
	key += key << 6;
	key ^= key >> 22;
	return (unsigned int)(key % sz);
}


static int h_search(void *self, uint64_t va, int64_t *handle)
{
	int ret = 1;
	struct htbl_state *s = (struct htbl_state *)self;
	int sz = s->size;
	unsigned int p = s->hashfunc(va, sz);
	int i;
	for (i = 0; i < s->probe_limit; i++) {
		uint64_t eva = s->buf[(p + i) % sz].va;
		if (eva == va) {
			ret = 0;
			p = (p + i) % sz;
			break;
		} else if (eva == (uint64_t)-1) {
			p = (p + i) % sz;
			break;
		}
	}
	if (handle != NULL) {
		*handle = p + ((int64_t)i << 32);
	}
	return ret;
}

static uint64_t h_get(void *self, int64_t handle)
{
	struct htbl_state *s = (struct htbl_state *)self;
	unsigned int i = handle & LS_BITMASK(32);
	if (i < s->size) {
		return s->buf[i].pa;
	} else {
		return (uint64_t)-1;
	}
}

static void h_insert(void *self, uint64_t va, uint64_t pa, int64_t handle)
{
	struct htbl_state *s = (struct htbl_state *)self;
	unsigned int i = handle & LS_BITMASK(32);
	if (i < s->size) {
		s->buf[i].va = va;
		s->buf[i].pa = pa;
	}
}

static void h_clear(void *self)
{
	struct htbl_state *s = (struct htbl_state *)self;
	memset(s->buf, -1, s->size * sizeof(struct htbl_entry));
}


void *ramses_vtlb_hashtbl_create(unsigned int size, hashfunc_t hfunc, int probelimit)
{
	void *buf = malloc(sizeof(struct htbl_state) + size * sizeof(struct htbl_entry));
	if (buf != NULL) {
		struct htbl_state *s = buf;
		s->size = size;
		s->probe_limit = probelimit;
		s->hashfunc = hfunc;
		h_clear(s);
	}
	return buf;
}

void ramses_vtlb_hashtbl_register_funcs(struct VTLBucketFuncs *funcs)
{
	funcs->search = h_search;
	funcs->get = h_get;
	funcs->insert = h_insert;
	funcs->clear = h_clear;
}

void ramses_vtlb_hashtbl_destroy(void *self)
{
	free(self);
}
