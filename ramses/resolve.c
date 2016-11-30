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

#include <ramses/resolve.h>

#include <string.h>
#include <stdlib.h>


static int handle_cntrl(const char *s, struct MemorySystem *o)
{
	if (strcmp("naive_ddr3", s) == 0) {
		o->controller = MEMCTRL_NAIVE_DDR3;
	} else if (strcmp("naive_ddr4", s) == 0) {
		o->controller = MEMCTRL_NAIVE_DDR4;
	} else if (strcmp("intel_sandy", s) == 0) {
		o->controller = MEMCTRL_INTEL_SANDY_DDR3;
	} else if (strcmp("intel_ivy", s) == 0) {
		o->controller = MEMCTRL_INTEL_IVY_DDR3;
	} else if (strcmp("intel_haswell", s) == 0) {
		o->controller = MEMCTRL_INTEL_HASWELL_DDR3;
	} else {
		return 1;
	}
	return 0;
}

static int handle_route(const char *s, struct MemorySystem *o)
{
	if (strcmp("passthru", s) == 0) {
		o->router = PAROUTE_PASSTHRU;
	} else if (strcmp("x86_generic", s) == 0) {
		o->router = PAROUTE_X86_GENERIC;
	} else {
		return 1;
	}
	return 0;
}

static int handle_remap(const char *s, struct MemorySystem *o)
{
	if (strcmp("none", s) == 0) {
		o->dimm_remap = DIMM_REMAP_NONE;
	} else if (strcmp("r3x0", s) == 0) {
		o->dimm_remap = DIMM_REMAP_R3X0;
	} else if (strcmp("r3x21", s) == 0) {
		o->dimm_remap = DIMM_REMAP_R3X21;
	} else if (strcmp("r3x210", s) == 0) {
		o->dimm_remap = DIMM_REMAP_R3X210;
	} else {
		return 1;
	}
	return 0;
}

static int handle_routeopt(char *s, struct MemorySystem *o)
{
	if (o->router != PAROUTE_PASSTHRU) {
		uint32_t asz = ramses_router_argsize(o->router);
		struct SysMemMapOpts *smm = malloc(sizeof(struct SysMemMapOpts) +
		                                   asz * sizeof(physaddr_t));
		if (smm == NULL) {
			return 1;
		}
		smm->argsize = asz;
		if (o->router == PAROUTE_X86_GENERIC) {
			sscanf(s, "%u,%lu,%lu", &smm->flags,
			                        &smm->args[SMM_ARG_X86_PCISTART],
			                        &smm->args[SMM_ARG_X86_TOPOFMEM]);
		} else {
			free(smm);
			return 1;
		}
		o->route_opts = smm;
	} else {
		return 1;
	}
	return 0;
}

static int handle_cntrlopt(char *s, struct MemorySystem *o)
{
	enum MemController c = o->controller;
	if (c == MEMCTRL_INTEL_IVYHASWELL_DDR3 || c == MEMCTRL_INTEL_SANDY_DDR3) {
		struct IntelCntrlOpts *ctrlo = malloc(sizeof(struct IntelCntrlOpts));
		ctrlo->flags = 0;

		if (strcmp("rank_mirror", s) == 0) {
			ctrlo->flags |= MEMCTRLOPT_INTEL_RANKMIRROR;
		} else {
			free(ctrlo);
			return 1;
		}
		o->controller_opts = ctrlo;
	} else {
		return 1;
	}
	return 0;
}

static int handle_line(char *line, ssize_t llen, struct MemorySystem *output, FILE *err)
{
	if (llen <= 1 || line[0] == '#') {
		return 0;
	}
	char cmd[llen];
	char arg[llen];
	sscanf(line, "%s %s", cmd, arg);
	if (strcmp("cntrl", cmd) == 0) {
		if (handle_cntrl(arg, output)) {
			if (err) fprintf(err, "Controller error: `%s'\n", arg);
			return 1;
		}
	} else if (strcmp("route", cmd) == 0) {
		if (handle_route(arg, output)) {
			if (err) fprintf(err, "Route error: `%s'\n", arg);
			return 2;
		}
	} else if (strcmp("remap", cmd) == 0) {
		if (handle_remap(arg, output)) {
			if (err) fprintf(err, "Remap error: `%s'\n", arg);
			return 4;
		}
	} else if (strcmp("route_opts", cmd) == 0) {
		if (handle_routeopt(arg, output)) {
			if (err) fprintf(err, "Route options error: `%s'\n", arg);
			return 8;
		}
	} else if (strcmp("cntrl_opts", cmd) == 0) {
		if (handle_cntrlopt(arg, output)) {
			if (err) fprintf(err, "Controller options error: `%s'\n", arg);
			return 32;
		}
	} else if (strcmp("chan", cmd) == 0) {
		output->mem_geometry |= MEMGEOM_CHANSELECT;
	} else if (strcmp("dimm", cmd) == 0) {
		output->mem_geometry |= MEMGEOM_DIMMSELECT;
	} else if (strcmp("rank", cmd) == 0) {
		output->mem_geometry |= MEMGEOM_RANKSELECT;
	} else {
		if (err) fprintf(err, "Unknown command: `%s'\n", cmd);
		return 16;
	}
	return 0;
}


int ramses_memsys_load_file(FILE *f, struct MemorySystem *output, FILE *err)
{
	char *line = NULL;
	size_t lblen = 0;
	int ret = 0;
	ssize_t llen;

	memset(output, 0, sizeof(*output));
	while ((llen = getline(&line, &lblen, f)) != -1) {
		ret |= handle_line(line, llen, output, err);
	}
	return ret;
}

int ramses_memsys_load_str(char *s, size_t slen, struct MemorySystem *output, FILE *err)
{
	int ret = 0;
	char *line, *ptr;
	char sb[slen + 1];
	strncpy(sb, s, slen);
	sb[slen] = '\0';

	memset(output, 0, sizeof(*output));
	line = strtok_r(sb, "\n", &ptr);
	do {
		ret |= handle_line(line, ptr - line - 1, output, err);
		line = strtok_r(NULL, "\n", &ptr);
	} while (line);
	return ret;
}

void ramses_memsys_free(struct MemorySystem *s)
{
	if (s->route_opts)
		free(s->route_opts);
	if (s->controller_opts)
		free(s->controller_opts);
}

int ramses_memsys_setup_x86(enum MemController ctrl, int geom_flags, void *ctrlopt,
                            memaddr_t ramsize, physaddr_t pcistart, int intelme,
                            enum DIMMRemap remap, struct MemorySystem *out)
{
	if (ramsize <= ramses_max_memory(ctrl, geom_flags, ctrlopt)) {
		/* RAM "fits" inside the geometry */
		struct SysMemMapOpts *smm = malloc(sizeof(smm) + SMM_ARGSIZE_X86_GENERIC * sizeof(physaddr_t));
		if (smm == NULL) {
			return 1;
		}
		smm->argsize = SMM_ARGSIZE_X86_GENERIC;
		smm->flags = SMM_FLAG_X86_REMAP | ((intelme) ? SMM_FLAG_X86_INTEL_ME : 0);
		smm->args[SMM_ARG_X86_PCISTART] = pcistart;
		smm->args[SMM_ARG_X86_TOPOFMEM] = ramsize;
		out->controller = ctrl;
		out->router = PAROUTE_X86_GENERIC;
		out->dimm_remap = remap;
		out->mem_geometry = geom_flags;
		out->route_opts = smm;
		out->controller_opts = ctrlopt;
		return 0;
	} else {
		return 1;
	}
}

struct DRAMAddr ramses_resolve(struct MemorySystem *s, physaddr_t addr)
{
	return (
		ramses_remap(
			s->dimm_remap,
			ramses_map(
				s->controller,
				ramses_route(s->router, addr, s->route_opts),
				s->mem_geometry,
				s->controller_opts
			)
		)
	);
}

physaddr_t ramses_resolve_reverse(struct MemorySystem *s, struct DRAMAddr addr)
{
	return (
		ramses_route_reverse(
			s->router,
			ramses_map_reverse(
				s->controller,
				ramses_remap_reverse(s->dimm_remap, addr),
				s->mem_geometry,
				s->controller_opts
			),
			s->route_opts
		)
	);
}
