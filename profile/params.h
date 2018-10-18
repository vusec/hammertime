/*
 * Copyright (c) 2018 Vrije Universiteit Amsterdam
 *
 * This program is licensed under the GPL2+.
 */

#ifndef PARAMS_H
#define PARAMS_H 1

#include "hammer.h"

#include <ramses/types.h>

#include <stddef.h>

enum run_mode {
	M_SINGLE = 0,
	M_DOUBLE = 1
};

struct ProfileParams {
	size_t alloc_sz;
	size_t alloc_al;
	int alloc_flags;

	int heur_bits;
	physaddr_t heur_base;
	char *msys_arg;
	char *msys_str;
	char *outfile;

	char *pres_opt;
	hammerfunc_t hamfunc;
	long hamopt;

	long width;
	long dist;
	long ints;
	long refresh;
	long cal;

	const void *tpat;
	const void *vpat;
	size_t tpatlen;
	size_t vpatlen;
	int invert_pat;

	enum run_mode mode;
	int incomplete;
	int dry_run;
	int verbosity;
};

#define V_ERR 0
#define V_INFO 1
#define V_DEBUG 2

extern const char USAGE_STR[];

int process_argv(int argc, char *argv[], struct ProfileParams *params);

#endif /* params.h */
