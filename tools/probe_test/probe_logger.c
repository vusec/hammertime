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

#include "probe_logger.h"

#include <ramses/types.h>

#include <stdlib.h>
#include <stdio.h>

#include <pthread.h>

int log_probe_timing(struct ProbeOutput *pout, FILE *f)
{
	uint64_t cur = 0, head = 0;
	uint64_t sz = pout->data_size;
	uint8_t *dp = pout->data;

	for (;;) {
		if (probeout_read_head(pout, cur, &head)) {
			break;
		}
		if (head - cur > sz) {
			fprintf(f, "Probe buffer loss!\n");
			cur = head;
			continue;
		}
		while (cur < head) {
			uint64_t ent;
			ent = *(uint64_t *)(dp + (cur % sz));
			cur += sizeof(uint64_t);
			if (pout->fmtflags & PROBEOUT_VIRTADDR) {
				cur += sizeof(uint64_t);
			}
			if (pout->fmtflags & PROBEOUT_OPSTATS) {
				cur += sizeof(struct MemOpStats);
			}
			if (ent == RAMSES_BADADDR) {
				int64_t t = *(int64_t *)(dp + (cur % sz));
				cur += sizeof(int64_t);
				if (t > 0) {
					fprintf(f, "TS %20ld (H 0x%lx)\n", t, head);
				} else {
					fprintf(f, "TD %20ld (H 0x%lx)\n", -t, head);
				}
			} else {
				/* Don't log data; WAY too much I/O work */
				//~ fprintf(f, "%016lx ", ent);
			}
		}
	}
	fputc('\n', f);
	return 0;
}
