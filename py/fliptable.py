#!/usr/bin/env python
# -*- coding: UTF-8 -*-
# Copyright (c) 2016 Andrei Tatar
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

import math
import struct

from collections import namedtuple

Flip = namedtuple('Flip', ['addr', 'cell_byte', 'pullup', 'pulldown'])
Hammering = namedtuple('Hammering', ['nflips', 'idx'])
Range = namedtuple('Range', ['start', 'nhammers', 'idx'])
Range._update_counts = lambda s, x: Range(start=s.start, nhammers=x - s.idx, idx=s.idx)

FTFlip = struct.Struct('BBBBHH HBB')
FTHammering = struct.Struct('II')
FTRange = struct.Struct('BBBBHH II')
FTHeader = struct.Struct('IIQQQQIII')

FLIPTBL_FILE_MAGIC = 0xf11b7ab1
FLIPTBL_FILE_ALIGN = 0x80

def _align(n):
    return math.ceil(n / FLIPTBL_FILE_ALIGN) * FLIPTBL_FILE_ALIGN


class Fliptable:

    def __init__(self, dist, ranges, hammers, flips):
        self.dist = dist
        self.ranges = ranges
        self.hammers = hammers
        self.flips = flips

    def write(self, out_filename):
        rangesz = len(self.ranges) * FTRange.size
        hammersz = len(self.hammers) * FTHammering.size
        flipsz = len(self.flips) * FTFlip.size
        rangeoff = _align(FTHeader.size)
        hammeroff = _align(rangeoff + rangesz)
        flipoff = _align(hammeroff + hammersz)
        size = flipoff + flipsz

        with open(out_filename, 'wb') as f:
            f.write(FTHeader.pack(FLIPTBL_FILE_MAGIC, self.dist, size, rangeoff, hammeroff,
                                  flipoff, len(self.ranges), len(self.hammers), len(self.flips)))
            f.seek(rangeoff)
            f.write(b''.join(FTRange.pack(*(x.start[:] + x[1:])) for x in self.ranges))
            f.seek(hammeroff)
            f.write(b''.join(FTHammering.pack(*x) for x in self.hammers))
            f.seek(flipoff)
            f.write(b''.join(FTFlip.pack(*(x.addr[:] + x[1:])) for x in self.flips))

    def __iter__(self):
        for r in self.ranges:
            for hi, ham in enumerate(self.hammers[x] for x in range(r.idx, r.idx + r.nhammers)):
                yield ((r.start.add_row(hi), r.start.add_row(hi + self.dist)),
                       self.flips[ham.idx : ham.idx + ham.nflips])
