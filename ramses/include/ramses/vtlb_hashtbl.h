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

/* Hash table implementation of the VTLBucket interface */

#ifndef _HAMTIME_RAMSES_VTLHASH_H
#define _HAMTIME_RAMSES_VTLHASH_H 1

#include <ramses/vtlbucket.h>

typedef unsigned int (*hashfunc_t)(uint64_t key, unsigned int sz);

/* Trivial modulo hash function */
unsigned int ramses_hash_trivial(uint64_t key, unsigned int sz);
/* Thomas Wang's 64bit hash function */
unsigned int ramses_hash_twang6432(uint64_t key, unsigned int sz);

void *ramses_vtlb_hashtbl_create(unsigned int size, hashfunc_t hfunc, int probelimit);
void ramses_vtlb_hashtbl_destroy(void *htbl);
void ramses_vtlb_hashtbl_register_funcs(struct VTLBucketFuncs *funcs);

#endif /* vtlb_hashtbl.h */
