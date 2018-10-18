/*
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

#include "calibrate_time.h"
#include "calibrate.h"

static const unsigned long DEFAULT_GRAN = 0x100000;
static const unsigned int DEFAULT_MULT = 3;
static const int OVRSHOOT_NUM = 1;
static const int OVRSHOOT_DEN = 32;

unsigned long calibrate_hammer_advanced(hammerfunc_t hammer_func, long hammer_opt,
                                        const uintptr_t a, const uintptr_t b,
                                        unsigned int dram_refresh_period_us,
                                        unsigned int max_overshoot_us,
                                        unsigned long start_gran,
                                        unsigned long measurement_mult)
{
	unsigned long retval = 0;
	unsigned long gran = start_gran;
	timeval_t t0, t;

	while (gran) {
		get_time(&t0);
		hammer_func(a, b, (retval + gran) * measurement_mult, hammer_opt);
		get_time(&t);

		long long diff_us = tdiff_us(t0, t) / measurement_mult;
		if (diff_us < dram_refresh_period_us) {
			retval += gran;
			continue;
		} else if (diff_us < dram_refresh_period_us + max_overshoot_us) {
			retval += gran;
			break;
		}
		gran >>= 1;
	}

	return retval;
}

unsigned long calibrate_hammer(hammerfunc_t hammer_func, long hammer_opt,
                               const uintptr_t a, const uintptr_t b,
                               unsigned int dram_refresh_period_us)
{
	return calibrate_hammer_advanced(
		hammer_func, hammer_opt, a, b,
		dram_refresh_period_us,
		(dram_refresh_period_us * OVRSHOOT_NUM) / OVRSHOOT_DEN,
		DEFAULT_GRAN, DEFAULT_MULT
	);
}
