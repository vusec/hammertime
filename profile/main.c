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

#include "alloc.h"
#include "bufmap_setup.h"
#include "calibrate.h"
#include "profile.h"
#include "pressure.h"
#include "params.h"

#include <ramses/msys.h>
#include <ramses/map.h>

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

static int VERBOSITY = V_ERR;


static void print_entry(struct AddrEntry e, FILE *f, const char *trail)
{
	struct DRAMAddr d = e.dramaddr;
	if (VERBOSITY & V_DEBUG) {
		size_t addr;
		#ifdef ADDR_DEBUG
		addr = e.physaddr;
		#else
		addr = e.virtp;
		#endif
		fprintf(f, "0x%zx " DRAMADDR_HEX_FMTSTR " ", addr,
		        d.chan, d.dimm, d.rank, d.bank, d.row, d.col);
	} else {
		fprintf(f, DRAMADDR_HEX_FMTSTR " ", d.chan, d.dimm, d.rank, d.bank, d.row, d.col);
	}
	if (trail) {
		fputs(trail, f);
	}
}

/* Profiling callbacks */

static int atk_check(struct AddrEntry a, struct AddrEntry b, void *arg)
{
	FILE *out = (FILE *)arg;
	print_entry(a, out, NULL);
	print_entry(b, out, ": ");
	return 0;
}

static void atk_end(void *arg)
{
	FILE *out = (FILE *)arg;
	fputc('\n', out);
}

static void bitflip_rep(struct AddrEntry a, size_t off, uint8_t exp,
                        uint8_t got, void *arg)
{
	FILE *out = (FILE *)arg;
	print_entry(a, out, NULL);
	fprintf(out, "%04zx|%02hhx|%02hhx ", off, got, exp);
}

/* Configuration and setup utility functions */

static int get_xbuf(struct BufferMap *bm, int aflags, void **xbuf, size_t *xlen)
{
	const int XBM_HEADROOM = 4;
	struct Mapping *map = &bm->msys->mapping;
	struct MappingProps p = map->props;
	size_t xsz = p.cell_size * p.col_cnt * p.bank_cnt * XBM_HEADROOM;
	if (ramses_map_twiddle_gran(map, (struct DRAMAddr){.chan=1})) xsz <<= 1;
	if (ramses_map_twiddle_gran(map, (struct DRAMAddr){.dimm=1})) xsz <<= 1;
	if (ramses_map_twiddle_gran(map, (struct DRAMAddr){.rank=1})) xsz <<= 1;

	aflags &= ~(0xff << ALLOC_HUGE_SHIFT);
	aflags |= ALLOC_HUGE_2MB;
	*xbuf = alloc_hammerbuf(xsz, 0, aflags);
	if (!*xbuf) {
		aflags &= ~(0xff << ALLOC_HUGE_SHIFT);
		*xbuf = alloc_hammerbuf(xsz, 0, aflags);
		if (!*xbuf) {
			return 1;
		}
	}
	*xlen = xsz;
	return 0;
}

static int get_xbm(struct BufferMap *bm, int aflags, struct BufferMap *xbm,
                   enum TransType trans, void *targ)
{
	size_t xsz;
	void *xbuf;
	if (get_xbuf(bm, aflags, &xbuf, &xsz)) {
		return 1;
	}
	if (setup_bufmap(xbm, xbuf, xsz, bm->msys, trans, targ)) {
		free_hammerbuf(xbuf, xsz);
		return 1;
	} else {
		return 0;
	}
}

static int config_pressure(struct BufferMap *bm, char *opt,
                           struct PressureKernel *pks, size_t pkslen)
{
	char *c = opt;
	size_t i = 0;
	while (*c != '\0' && i < pkslen - 1) {
		switch (*c) {
			case 'S':
			case 's':
			{
				pks[i].func = (*c == 's') ? PRES_SEQ_LD : PRES_SEQ_ST;
				if (get_xbuf(bm, 0, &(pks[i].arg.buf), &(pks[i].arg.buflen))) {
					return 1;
				}
				c++;
				pks[i].arg.step = 1 << strtol(c, &c, 10);
				i++;
			}
				break;
			default:
				return 1;
		}
	}
	pks[i].func = PRES_NONE;
	return 0;
}

static int cleanup_pressure(struct PressureKernel *pks)
{
	int r;
	for (size_t i = 0; pks[i].func != PRES_NONE; i++) {
		if ((r = free_hammerbuf(pks[i].arg.buf, pks[i].arg.buflen))) {
			return r;
		}
	}
	return 0;
}

static unsigned long calibrate(hammerfunc_t hamfunc, long hamopt,
                               struct BufferMap *bm, long refresh)
{
	int r;
	struct AddrEntry ae[2];
	struct BMPos p1 = { 0 , 0 };
	struct BMPos p2 = ramses_bufmap_next(bm, p1, DRAM_ROW);
	r = ramses_bufmap_get_entry(bm, p1, &ae[0]);
	assert(!r);
	r = ramses_bufmap_get_entry(bm, p2, &ae[1]);
	assert(!r);
	return calibrate_hammer(hamfunc, hamopt, ae[0].virtp, ae[1].virtp, refresh);
}

/* Stream/file readin functions */

static char *read_stream(FILE *f)
{
	const size_t BUFSZ = 4095;
	char *buf, *bufp;
	size_t sz = BUFSZ;
	size_t toread = sz;
	size_t read = 0;
	if ((buf = malloc(sz + 1)) == NULL) {
		return NULL;
	}
	bufp = buf;
	/* Fill increasingly large buffers */
	while ((read = fread(bufp, 1, toread, f)) == toread &&
	        !feof(f))
	{
		size_t newsz = (sz + 1) * 2 - 1;
		char *newbuf = realloc(buf, newsz + 1);
		if (newbuf == NULL) {
			free(buf);
			return NULL;
		}
		bufp = newbuf + sz;
		toread = newsz - sz;
		buf = newbuf;
		sz = newsz;
	}
	if (ferror(f)) {
		free(buf);
		return NULL;
	}
	buf[sz - toread + read] = '\0';
	return buf;
}

static char *read_file(FILE *f)
{
	char *buf;
	size_t sz;
	if (fseek(f, 0L, SEEK_END)) {
		return NULL;
	}
	sz = ftell(f);
	if ((buf = malloc(sz + 1)) == NULL) {
		return NULL;
	}
	rewind(f);
	if (fread(buf, 1, sz, f) != sz) {
		free(buf);
		return NULL;
	}
	buf[sz] = '\0';
	return buf;
}


/* Showtime */
int main(int argc, char *argv[])
{
	int r;
	void *buf;
	FILE *outf = stdout;
	char *msys_str;

	struct ProfileParams p;
	struct MemorySystem msys;
	struct BufferMap bm, xbm;
	struct ProfileCtx pc = {0};

	const int MAX_PRES = 9;
	struct PressureKernel pks[MAX_PRES];
	char pres_threads[pres_tids_size(MAX_PRES)];

	struct HeurArg harg;
	enum TransType trans = TRANS_NATIVE;
	void *targ = NULL;

	switch (process_argv(argc, argv, &p)) {
		case 0: break;
		case 1: return 0;
		case 2: goto err_usage;
		default: goto err_out;
	}
	VERBOSITY = p.verbosity;
	if (p.heur_bits) {
		harg.bits = p.heur_bits;
		harg.base = p.heur_base;
		trans = TRANS_HEUR;
		targ = &harg;
	}

	/* MSYS init */
	msys_str = p.msys_str;
	if (msys_str == NULL) {
		assert(p.msys_arg);
		FILE *msysf;
		if (p.msys_arg[0] == '-' && !p.msys_arg[1]) {
			msysf = stdin;
			if ((msys_str = read_stream(msysf)) == NULL) {
				perror("Error reading msys from stdin");
				goto err_out;
			}
		} else {
			if ((msysf = fopen(p.msys_arg, "r")) == NULL) {
				perror("Error opening msys file");
				goto err_out;
			}
			if ((msys_str = read_file(msysf)) == NULL) {
				perror("Error reading msys file");
				goto err_out;
			}
		}
		fclose(msysf);
	}
	if ((r = ramses_msys_load(msys_str, &msys, NULL))) {
		fprintf(stderr, "Error loading msys: %s\n", ramses_msys_load_strerr(r));
		goto err_out;
	}
	if (p.msys_str == NULL) {
		free(msys_str);
	}

	/* OUTFILE init */
	if (p.outfile != NULL) {
		if ((outf = fopen(p.outfile, "w")) == NULL) {
			perror("Error opening output file");
			goto err_usage;
		}
	}

	/* Target setup */
	if (VERBOSITY & V_INFO) {
		fprintf(stderr, "Running %s rowhammer\n",
		        (p.mode == M_SINGLE) ? "single-sided" : "double-sided");
		fprintf(stderr, "Setting up targets (size %zu buffer)... ", p.alloc_sz);
	}
	buf = alloc_hammerbuf(p.alloc_sz, p.alloc_al, p.alloc_flags);
	if (buf == NULL) {
		perror("Error allocating hammer buffer");
		goto err_out;
	}
	if (setup_bufmap(&bm, buf, p.alloc_sz, &msys, trans, targ)) {
		perror("Error setting up buffer map");
		goto err_out;
	}
	if (VERBOSITY & V_DEBUG) {
		fprintf(stderr, "@ %p ", buf);
	}
	if (VERBOSITY & V_INFO) {
		fputs("done\n", stderr);
	}
	/* Extra buffer setup */
	if (p.mode == M_SINGLE && get_xbm(&bm, p.alloc_flags, &xbm, trans, targ)) {
		fputs("Error setting up extra buffer\n", stderr);
		goto err_out;
	}

	/* Pressure setup */
	if (p.pres_opt) {
		if (config_pressure(&bm, p.pres_opt, pks, MAX_PRES)) {
			fputs("Invalid pressure argument\n", stderr);
			goto err_usage;
		}
		/* Start pressure threads */
		if (pres_start(pks, pres_threads)) {
			perror("Error creating pressure thread(s)");
			goto err_out;
		}
	}

	/* Prepare profile context */
	pc = (struct ProfileCtx) {
		.bm = &bm,
		.tpat = p.tpat,
		.tpatlen = p.tpatlen,
		.vpat = p.vpat,
		.vpatlen = p.vpatlen,
		.hamfunc = p.hamfunc,
		.hamopt = p.hamopt,
		.attack_check_fn = atk_check,
		.attack_check_fn_arg = outf,
		.bitflip_report_fn = bitflip_rep,
		.bitflip_report_fn_arg = outf,
		.attack_end_fn = atk_end,
		.attack_end_fn_arg = outf,
		.extra = (p.mode == M_SINGLE) ? &xbm : NULL,
		.cal = p.cal,
		.cal_mult = p.ints,
		.width = p.width,
		.dist = p.dist,
		.invert_pat = p.invert_pat,
		.incomplete = p.incomplete,
	};

	/* Calibration */
	if (!p.cal) {
		if (VERBOSITY & V_INFO) {
			fprintf(stderr, "Refresh interval is %lu ms\n", p.refresh / 1000);
			fputs("Calibrating... ", stderr);
		}
		pc.cal = calibrate(p.hamfunc, p.hamopt, &bm, p.refresh);
	} else {
		fputs("Calibration override: ", stderr);
	}
	if (VERBOSITY & V_INFO) {
		fprintf(stderr, "%lu hammers per refresh interval\n", pc.cal);
	}

	/* Run profile */
	if (!p.dry_run) {
		if (VERBOSITY & V_INFO) {
			fputs("Profiling...\n", stderr);
		}
		switch (p.mode) {
			case M_SINGLE: profile_singlesided(&pc); break;
			case M_DOUBLE: profile_doublesided(&pc); break;
			default: fputs("Error: unknown run mode", stderr); goto err_out;
		}
	} else {
		fputs("DRY RUN -- no work to be done\n", stderr);
	}
	if (VERBOSITY & V_INFO) {
		fputs("Finished\n", stderr);
	}

	/* Cleanup */
	if (p.pres_opt) {
		if (pres_stop(pks, pres_threads)) {
			perror("Error stopping pressure thread(s)");
			goto err_out;
		}
		if (cleanup_pressure(pks)) {
			perror("Error cleaning up pressure thread(s)");
			goto err_out;
		}
	}
	if (p.mode == M_SINGLE) {
		ramses_bufmap_free(&xbm);
		free_hammerbuf(xbm.bufbase, xbm.page_size * xbm.pte_cnt);
	}
	ramses_bufmap_free(&bm);
	ramses_msys_free(&msys);
	free_hammerbuf(buf, p.alloc_sz);
	return 0;

	/* Error conditions */
	err_usage:
		fprintf(stderr, USAGE_STR, argv[0]);
	err_out:
		return 1;
}
