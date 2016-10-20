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

#ifndef _HAMTIME_RAMSES_BITOPS_H
#define _HAMTIME_RAMSES_BITOPS_H 1

#define LS_BITMASK(n) ((1UL << (n)) - 1)
#define BIT(n,x) (((x) >> (n)) & 1)
#define POP_BIT(n,x) (((x) & LS_BITMASK(n)) + (((x) >> ((n)+1)) << (n)))

#endif /* bitops.h */
