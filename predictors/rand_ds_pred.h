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

/*
 * A (very) simple demo predictor that randomly generates bitflips on access
 * patterns naively resembling doublesided rowhammering
 */

#ifndef _HAMTIME_RAND_DS_PRED_H
#define _HAMTIME_RAND_DS_PRED_H 1

#include "predictor.h"

/*
 * Set up the context and functions for the predictor.
 * chance is the probability of a bitflip; P = chance/RAND_MAX;
 */
int init_rand_ds_predictor(struct Predictor *p, int chance);

#endif /* random_doublesided.h */
