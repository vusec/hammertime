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

#include "fliptable.h"

#include <ramses/util.h>

#include <unistd.h>
#include <sys/mman.h>

static const uint32_t FLIPTBL_FILE_MAGIC = 0xf11b7ab1;

struct FTFileHdr {
	uint32_t magic;
	uint32_t dist;
	uint64_t size;
	uint64_t rangetbl_off;
	uint64_t hammertbl_off;
	uint64_t fliptbl_off;
	uint32_t num_ranges;
	uint32_t num_hammers;
	uint32_t num_flips;
};

static inline size_t bitsize(size_t x)
{
	size_t ret = 1;
	while (ret <= x) ret <<= 1;
	return ret;
}

static inline uint32_t extrap_row(struct FlipTable *ft, struct DRAMAddr addr,
                                  struct Flip **flips, struct Range *r,
                                  enum ExtrapMode extrap,
                                  struct DRAMAddr *extrap_diff)
{
	uint32_t rsz;
	int d;
	switch (extrap) {
		case EXTRAP_PERBANK_TRUNC:
			rsz = bitsize(r->num_hammers);
			rsz >>= 1;
			break;
		case EXTRAP_PERBANK:
			rsz = r->num_hammers;
			break;
		case EXTRAP_PERBANK_FIT:
			rsz = bitsize(r->num_hammers);
			if (4 * r->num_hammers < 3 * rsz) {
				rsz >>= 1;
			}
			break;
		default:
			return 0;
	}
	if (extrap == EXTRAP_PERBANK_FIT) {
		uint32_t mask = rsz - 1;
		int adj = r->start.row & mask;
		struct DRAMAddr vstart = r->start;
		vstart.row &= ~mask;
		d = ramses_dramaddr_rowdiff(addr, vstart) % rsz;
		if (d < adj || d > adj + r->num_hammers) {
			/* Extrapolation fell outside the fitted virtual range */
			return 0;
		} else {
			d -= adj;
		}
	} else {
		d = ramses_dramaddr_rowdiff(addr, r->start) % rsz;
	}
	struct Hammering h = ft->hammer_tbl[r->ham_idx + d];
	*flips = &ft->flip_tbl[h.flip_idx];
	if (extrap_diff) {
		*extrap_diff = ramses_dramaddr_diff(addr,
		                                    ramses_dramaddr_addrows(r->start, d));
	}
	return h.num_flips;
}

uint32_t fliptbl_lookup(struct FlipTable *ft, struct DRAMAddr addr,
                        enum ExtrapMode extrap, struct Flip **flips,
                        struct DRAMAddr *extrap_diff)
{
	uint32_t p = 0;
	uint32_t left, right;
	left = ft->num_ranges / 2;
	right = ft->num_ranges / 2 + (ft->num_ranges % 2);
	while (right) {
		int idx = p + left;
		struct DRAMAddr s = ft->range_tbl[idx].start;
		if (ramses_same_bank(addr, s)) {
			int d = ramses_dramaddr_rowdiff(addr, s);
			if (d > 0 && d < ft->range_tbl[idx].num_hammers) {
				struct Hammering h = ft->hammer_tbl[ft->range_tbl[idx].ham_idx + d];
				*flips = &ft->flip_tbl[h.flip_idx];
				if (extrap_diff) {
					struct DRAMAddr ed = {0, 0, 0, 0, 0, 0};
					*extrap_diff = ed;
				}
				return h.num_flips;
			}
		}
		if (ramses_dramaddr_cmp(addr, s) > 0) {
			p = idx;
			left = right / 2;
			if (right == 1) {
				right = 0;
			} else {
				right = right / 2 + (right % 2);
			}
		} else {
			right = (left / 2) + (left % 2);
			left = left / 2;
		}
	}
	switch (extrap) {
		case EXTRAP_PERBANK:
		case EXTRAP_PERBANK_TRUNC:
		case EXTRAP_PERBANK_FIT:
			if (ramses_same_bank(ft->range_tbl[p].start, addr))
				return extrap_row(ft, addr, flips, &ft->range_tbl[p],
				                  extrap, extrap_diff);
			if (ramses_same_bank(ft->range_tbl[p + 1].start, addr))
				return extrap_row(ft, addr, flips, &ft->range_tbl[p + 1],
				                  extrap, extrap_diff);
		case EXTRAP_NONE:
		default:
			return 0;
	}
}

int fliptbl_load(int fd, struct FlipTable *ft)
{
	struct FTFileHdr hdr;
	uint8_t *buf;
	if (pread(fd, &hdr, sizeof(hdr), 0) != sizeof(hdr)) {
		return FLIPTBL_ERR_FILE;
	}
	if (hdr.magic != FLIPTBL_FILE_MAGIC) {
		return FLIPTBL_ERR_MAGIC;
	}
	buf = mmap(NULL, hdr.size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (buf == MAP_FAILED) {
		return FLIPTBL_ERR_MMAP;
	}
	ft->dist = hdr.dist;
	ft->num_ranges = hdr.num_ranges;
	ft->mmap = buf;
	ft->range_tbl = (struct Range *)(buf + hdr.rangetbl_off);
	ft->hammer_tbl = (struct Hammering *)(buf + hdr.hammertbl_off);
	ft->flip_tbl = (struct Flip *)(buf + hdr.fliptbl_off);
	return 0;
}

int fliptbl_close(struct FlipTable *ft)
{
	struct FTFileHdr *hdr = (struct FTFileHdr *)ft->mmap;
	return munmap(hdr, hdr->size);
}
