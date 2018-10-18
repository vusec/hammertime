/*
 * Copyright (c) 2016 Andrei Tatar
 * Copyright (c) 2017-2018 Vrije Universiteit Amsterdam
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

#include "params.h"
#include "alloc.h"

#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>

#include <getopt.h>
#include <libgen.h>


static const char INTRO_STR[] =
	"Hammertime profile --- a memory addressing aware rowhammer probing tool\n"
;
const char USAGE_STR[] =
	"Usage: %s [-hvdHitL] [-a ALIGNMENT] [-W WIDTH] [-D DISTANCE] "
	"[-I REFRESH_INTERVALS] [-R REFRESH_DURATION_US] "
	"[-C CALIBRATION_OVERRIDE] [-P PRESSURE_OPTIONS] "
	"[-T TARGET_PATTERN] [-V VICTIM_PATTERN] [-X BASE] "
	"[-K HAMMER_KERNEL] [--incomplete] [--single|--double] "
	"SIZE (-m MSYS_STRING | MSYS_FILE) [OUT_FILE]\n"
;
static const char HELP_STR[] =
"Numeric arguments are assumed to be decimal, unless prefixed by `0' or `0x' in which case octal or hexadecimal is assumed, respectively.\n"
"Positional arguments:\n"
"	SIZE		(Required) use SIZE bytes as the profiling buffer; can have 'K', 'M', 'G' or 'T' suffix (for KiB, MiB, GiB or TiB)\n"
"	MSYS_FILE	(Required if not using -m) path to file describing memory system. Use '-' to read from stdin\n"
"	OUT_FILE	if provided, will be used for output instead of stdout\n"
"Options:\n"
"	-m MSYS_STRING, --msys MSYS_STRING"
"	use MSYS_STRING as memory description instead of reading from file\n"
"	-h, --help"
"	print this help and exit\n"
"	-v, --verbose"
"	be more verbose. Can be specified multiple times to increase verbosity\n"
"	-d, --dry-run"
"	allocate all buffers and perform calibrations, but return before doing any hammering\n"
"	-H, --huge"
"	(Recommended) allocate buffers using hugetlb pages. Specify twice to force request 1GB pages\n"
"	-i, --invert-patterns"
"	also hammer with bit-inverted data patterns\n"
"	-t, --thp"
"	advise the system to use Transparent Huge Pages for the hammer buffer\n"
"	-L, --no-lock"
"	do not lock hammer buffer to RAM. Not recommended, as it can negatively affect accuracy; use only to get around ulimits\n"
"	-a ALIGNMENT, --align ALIGNMENT"
"	allocate hammer buffer aligned at ALIGNMENT boundaries in virtual address space\n"
"	-W WIDTH, --width=WIDTH"
"	check an additional WIDTH rows on each side of the aggressor row(s). Default: 1 (single-sided), 0 (double-sided)\n"
"	-D DIST, --dist=DIST"
"	set distance between dual-sided hammer targets to DIST rows. Default: 1, ignored when doing single-sided rowhammer\n"
"	-I COUNT, --intervals=COUNT"
"	hammer each row set for COUNT refresh intervals. Default: 3\n"
"	-R NUM, --refresh-interval=NUM"
"	assume a DRAM refresh interval of NUM milliseconds. Default: 64\n"
"	-C CAL, --calibration=CAL"
"	skip calibration and assume CAL hammers per refresh interval\n"
"	-P PRES, --pressure=PRES"
"	create pressure threads to simulate memory contention. PRES consists of one or more one-character pressure TYPE selectors, each optionally followed by a decimal SHIFT value. If SHIFT is missing it is assumed to be 0.\n"
"		TYPEs:\n"
"			's' - sequential reads using a 2^(SHIFT) increment\n"
"			'S' - sequential writes using a 2^(SHIFT) increment\n"
"	-V PATTERN, --victim-pattern=PATTERN"
"	set victim fill pattern to PATTERN. Expects a hexadecimal number of even length representing fill bytes. Default 'ff'\n"
"	-T PATTERN, --target-pattern=PATTERN"
"	set target fill pattern to PATTERN. Same rules apply as for -V. Default '00'\n"
"	-X BASE, --trans-heur=BASE"
"	heuristically translate virtual to physical addresses by taking the lower order bits of the address and adding them to BASE. SIZE is rounded down to nearest power of two, and the hammer buffer is size-aligned\n"
"	-K KERN, --hammer-kernel=KERN"
"	use KERN as main loop for performing rowhammer. Default ld_flush if available, ld otherwise\n"
"		KERNs:\n"
"			'ld' - repeated loads\n"
"		on x86:\n"
"			'ld_flush' - repeated loads preceded by cache flushing with clflush\n"
"			'ld_flush_mfence' - repeated loads preceded by cache flushing and an mfence\n"
"	--incomplete"
"	also hammer on incompletely mapped rows (WARNING: may cause memory corruption in other processes)\n"
"	--single | --double"
"	perform either single-sided or double-sided rowhammer, respecively. Default: double-sided\n"
;

/* Argument processing utility functions */

static int suffix2shift(char suffix)
{
	int shift = 0;
	switch (suffix) {
		case 't':
		case 'T':
			shift += 10;
		case 'g':
		case 'G':
			shift += 10;
		case 'm':
		case 'M':
			shift += 10;
		case 'k':
		case 'K':
			shift += 10;
			break;
	}
	return shift;
}

static int str2pat(const char *str, const void **pat, size_t *patlen)
{
	char *endp = NULL;
	char tmp[3];
	void *p;
	tmp[2] = '\0';
	size_t len = strlen(str);
	if (len % 2) {
		return EINVAL;
	}
	len /= 2;

	p = malloc(len);
	for (size_t i = 0; i < len; i++) {
		tmp[0] = str[2*i];
		tmp[1] = str[2*i + 1];
		errno = 0;
		((uint8_t *)p)[i] = (uint8_t) strtol(tmp, &endp, 16);
		if (errno) {
			free(p);
			return errno;
		}
		if (*endp != '\0') {
			free(p);
			return EINVAL;
		}
	}
	*patlen = len;
	*pat = p;
	return 0;
}

static int convert_arg_delim(char *opt, char *name, long *val, char delim)
{
	char *end = NULL;
	errno = 0;
	*val = strtol(opt, &end, 0);
	if (errno) {
		fputs("Cannot convert argument: ", stderr);
		perror(name);
		return 1;
	} else if (*end != delim) {
		fputs("Invalid argument: ", stderr);
		fputs(name, stderr);
		fputc('\n', stderr);
		return 2;
	}
	if (*val < 0) {
		fputs("Value of argument ", stderr);
		fputs(name, stderr);
		fputs(" must be positive\n", stderr);
		return 3;
	}
	return 0;
}

static int convert_arg(char *opt, char *name, long *val)
{
	return convert_arg_delim(opt, name, val, '\0');
}

static int convert_sufx(char *opt, char *name, size_t *val)
{
	char *end = NULL;
	size_t last = strlen(opt) - 1;
	int sh = suffix2shift(opt[last]);
	errno = 0;
	size_t v = strtol(opt, &end, 0);
	if (errno || end < &opt[last] || (end == &opt[last] && sh == 0)) {
		fputs("Invalid ", stderr);
		fputs(name, stderr);
		fputc('\n', stderr);
		return 1;
	}
	*val = v << sh;
	return 0;
}

static int get_hamfunc(char *opt, hammerfunc_t *func, long *hopt)
{
	if (!strcmp(opt, "ld")) {
		*func = hammer_ld;
	}
#if HAMMER_X86
	else if (!strcmp(opt, "ld_flush")) {
		*func = hammer_ld_flush;
	} else if (!strcmp(opt, "ld_flush_mfence")) {
		*func = hammer_ld_flush_mfence;
	}
#endif
	else {
		return 1;
	}
	return 0;
}

#define NOVAL (-1L)

static const unsigned long DRAM_REFRESH_US = 64000;
static const unsigned long DEFAULT_INTERVALS = 3;

static const uint8_t DEFAULT_TPAT[]  = {0x00};
static const size_t DEFAULT_TPATLEN = sizeof(DEFAULT_TPAT);
static const uint8_t DEFAULT_VPAT[]  = {0xff};
static const size_t DEFAULT_VPATLEN = sizeof(DEFAULT_VPAT);

int process_argv(int argc, char *argv[], struct ProfileParams *p)
{
	const struct option longopts[] = {
		{ .name="help", .has_arg=no_argument, .flag=NULL, .val='h' },
		{ .name="verbose", .has_arg=no_argument, .flag=NULL, .val='v' },
		{ .name="dry-run", .has_arg=no_argument, .flag=NULL, .val='d' },
		{ .name="huge", .has_arg=no_argument, .flag=NULL, .val='H' },
		{ .name="invert-patterns", .has_arg=no_argument, .flag=NULL, .val='i'},
		{ .name="thp", .has_arg=no_argument, .flag=NULL, .val='t' },
		{ .name="no-lock", .has_arg=no_argument, .flag=NULL, .val='L' },

		{ .name="msys", .has_arg=required_argument, .flag=NULL, .val='m' },
		{ .name="align", .has_arg=required_argument, .flag=NULL, .val='a' },
		{ .name="width", .has_arg=required_argument, .flag=NULL, .val='W' },
		{ .name="dist", .has_arg=required_argument, .flag=NULL, .val='D' },
		{ .name="intervals", .has_arg=required_argument, .flag=NULL, .val='I' },
		{ .name="target-pattern", .has_arg=required_argument, .flag=NULL, .val='T' },
		{ .name="victim-pattern", .has_arg=required_argument, .flag=NULL, .val='V' },
		{ .name="refresh-interval", .has_arg=required_argument, .flag=NULL, .val='R' },
		{ .name="calibration", .has_arg=required_argument, .flag=NULL, .val='C' },
		{ .name="pressure", .has_arg=required_argument, .flag=NULL, .val='P'},
		{ .name="hammer-kernel", .has_arg=required_argument, .flag=NULL, .val='K'},
		{ .name="trans-heur", .has_arg=required_argument, .flag=NULL, .val='X'},

		{ .name="incomplete", .has_arg=no_argument, .flag=(int *)&p->incomplete, .val=3 },
		{ .name="single", .has_arg=no_argument, .flag=(int *)&p->mode, .val=0 },
		{ .name="double", .has_arg=no_argument, .flag=(int *)&p->mode, .val=1 },
		{ 0, 0, 0, 0 }
	};
	int huge = 0;
	int heur = 0;
	size_t hbase;
	int opt;
	/* Defaults */
	*p = (struct ProfileParams) {
	#ifdef HAMMER_X86
		.hamfunc = hammer_ld_flush,
	#else
		.hamfunc = hammer_ld,
	#endif
		.width = NOVAL, .dist = 1, .ints = DEFAULT_INTERVALS,
		.refresh = DRAM_REFRESH_US,
		.tpat = DEFAULT_TPAT, .tpatlen = DEFAULT_TPATLEN,
		.vpat = DEFAULT_VPAT, .vpatlen = DEFAULT_VPATLEN,
		.mode = M_DOUBLE, .incomplete = 0
	};
	/* Process args */
	while ((opt = getopt_long(argc, argv, "hvdHitLm:a:W:D:I:T:V:R:C:P:X:K:",
	                          longopts, NULL)) != -1)
	{
		switch(opt) {
			case 0:
				break;
			case 'v':
				p->verbosity = (p->verbosity << 1) | 1;
				break;
			case 'm':
				p->msys_str = optarg;
				break;
			case 'd':
				p->dry_run |= 1;
				break;
			case 'H':
				huge++;
				break;
			case 'i':
				p->invert_pat |= 1;
				break;
			case 't':
				p->alloc_flags |= ALLOC_THP;
				break;
			case 'L':
				p->alloc_flags |= ALLOC_NOLOCK;
				break;
			case 'a':
				if (convert_sufx(optarg, "alignment", &p->alloc_al)) goto err_usage;
				break;
			case 'W':
				if (convert_arg(optarg, "width", &p->width)) goto err_usage;
				break;
			case 'D':
				if (convert_arg(optarg, "distance", &p->dist)) goto err_usage;
				break;
			case 'I':
				if (convert_arg(optarg, "intervals", &p->ints)) goto err_usage;
				break;
			case 'R':
			{
				long refresh;
				if (convert_arg(optarg, "refresh-interval", &refresh)) goto err_usage;
				p->refresh = refresh * 1000;
			}
				break;
			case 'C':
				if (convert_arg(optarg, "calibration", &p->cal)) goto err_usage;
				break;
			case 'P':
				p->pres_opt = optarg;
				break;
			case 'T':
				if (str2pat(optarg, &p->tpat, &p->tpatlen)) {
					fprintf(stderr, "Invalid target fill pattern: %s\n", optarg);
					goto err_usage;
				}
				break;
			case 'V':
				if (str2pat(optarg, &p->vpat, &p->vpatlen)) {
					fprintf(stderr, "Invalid victim fill pattern: %s\n", optarg);
					goto err_usage;
				}
				break;
			case 'X':
				if (convert_sufx(optarg, "heuristic translation base", &hbase)) goto err_usage;
				heur = 1;
				break;
			case 'K':
				if (get_hamfunc(optarg, &p->hamfunc, &p->hamopt)) {
					fprintf(stderr, "Invalid hamemr kernel: %s\n", optarg);
					goto err_usage;
				}
				break;
			case 'h':
				fputs(INTRO_STR, stderr);
				fprintf(stderr, USAGE_STR, argv[0]);
				fputc('\n', stderr);
				fputs(HELP_STR, stderr);
				return 1;
			default:
				goto err_usage;
		}
	}
	/* SIZE positional arg */
	if (optind < argc) {
		if (convert_sufx(argv[optind], "buffer size", &p->alloc_sz)) {
			goto err_usage;
		}
		if (!p->alloc_sz) {
			fputs("SIZE must be non-zero\n", stderr);
			goto err_usage;
		}
		optind++;
	} else {
		fputs("Missing argument: SIZE\n", stderr);
		goto err_usage;
	}
	/* MSYS positional arg */
	if (p->msys_str == NULL) {
		if (optind == argc) {
			fprintf(stderr, "Missing argument: MSYS_FILE\n");
			goto err_usage;
		} else {
			p->msys_arg = argv[optind];
			optind++;
		}
	}
	/* OUTFILE positional arg */
	if (optind < argc) {
		p->outfile = argv[optind];
		optind++;
	}
	/* Final tweaks */
	if (p->width == NOVAL) {
		p->width = (p->mode == M_DOUBLE) ? 0 : 1;
	}
	if (huge >= HUGE_SHIFTS_LEN) {
		fputs("More '-H' or '--huge' options than supported\n", stderr);
		goto err_usage;
	} else {
		p->alloc_flags |= HUGE_SHIFTS[huge] << ALLOC_HUGE_SHIFT;
	}
	if (heur) {
		int sh = 0;
		for (size_t sz = p->alloc_sz; sz >>= 1; sh++);
		p->alloc_sz = (size_t)1 << sh;
		p->alloc_al = p->alloc_sz;
		p->heur_bits = sh;
		p->heur_base = (physaddr_t)hbase;
	}

	return 0;

	err_usage:
	return 2;
}
