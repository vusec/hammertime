/*
 * Copyright (c) 2017-2018 Vrije Universiteit Amsterdam
 *
 * This program is licensed under the GPL2+.
 */

#ifndef CALIBRATE_H
#define CALIBRATE_H 1

#include "hammer.h"

#include <stdint.h>

/*
 * Find out how many hammer iterations can happen in a DRAM refresh interval.
 *
 * Calibrates hammer_func with targets a and b for a DRAM refresh interval
 * of dram_refresh_period_us (in microseconds), with at most max_overshoot_us
 * of overshoot, starting at start_gran, and averaging the calibration for
 * measurement_mult refresh intervals.
 */
unsigned long calibrate_hammer_advanced(hammerfunc_t hammer_func, long hammer_opt,
                                        const uintptr_t a, const uintptr_t b,
                                        unsigned int dram_refresh_period_us,
                                        unsigned int max_overshoot_us,
                                        unsigned long start_gran,
                                        unsigned long measurement_mult);

/* Simplified calibration function, with sensible defaults */
unsigned long calibrate_hammer(hammerfunc_t hammer_func, long hammer_opt,
                              const uintptr_t a, const uintptr_t b,
                              unsigned int dram_refresh_period_us);

#endif /* calibrate.h */
