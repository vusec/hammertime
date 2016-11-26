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

from pyramses import DRAMAddr

Flip = namedtuple('Flip', ['addr', 'cell_byte', 'pullup', 'pulldown'])
Hammering = namedtuple('Hammering', ['nflips', 'idx'])
Range = namedtuple('Range', ['start', 'nhammers', 'idx'])
Range._update_counts = lambda s, x: Range(start=s.start, nhammers=x - s.idx, idx=s.idx)

Attack = namedtuple('Attack', ['targets', 'victims'])

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
        """Write out a binary representation suitable for use in compiled code"""
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

    def __eq__(self, other):
        return tuple(self) == tuple(other)

    def __iter__(self):
        for r in self.ranges:
            for hi, ham in enumerate(self.hammers[x] for x in range(r.idx, r.idx + r.nhammers)):
                yield Attack(
                    (r.start + DRAMAddr(row=hi), r.start + DRAMAddr(row=hi + self.dist)),
                    self.flips[ham.idx : ham.idx + ham.nflips]
                )

    def diff(self, other):
        """
        Run a diff operation between two fliptables.

        Return a 3-tuple of fliptables:
            [0] - bit flips unique to self
            [1] - bit flips common to both
            [2] - bit flips unique to other
        """
        if not isinstance(other, type(self)):
            raise ValueError('Fliptable required for diff')
        elif self.dist != other.dist:
            raise ValueError('Cannot compare fliptables with different dist values')
        uself = []
        uother = []
        common = []

        satks = iter(self)
        oatks = iter(other)
        sa = next(satks, None)
        oa = next(oatks, None)

        while sa is not None or oa is not None:
            # Degenerate cases
            if sa is None:
                uother.append(oa)
                oa = next(oatks, None)
                continue
            if oa is None:
                uself.append(sa)
                sa = next(satks, None)
                continue

            if sa.targets[0] < oa.targets[0]:
                uself.append(sa)
                sa = next(satks, None)
            elif sa.targets[0] > oa.targets[0]:
                uother.append(oa)
                oa = next(oatks, None)
            else:
                flips = sorted(set(sa.victims) | set(oa.victims),
                               key=lambda f: (f.addr.numeric_value << 8) + f.cell_byte)
                uself.append(Attack(
                    targets=sa.targets,
                    victims=[x for x in flips if x not in oa.victims]
                ))
                uother.append(Attack(
                    targets=oa.targets,
                    victims=[x for x in flips if x not in sa.victims]
                ))
                common.append(Attack(
                    targets=sa.targets,
                    victims=[x for x in flips if x in sa.victims and x in oa.victims]
                ))
                sa = next(satks, None)
                oa = next(oatks, None)

        return (Fliptable.from_attacks(uself), Fliptable.from_attacks(common), Fliptable.from_attacks(uother))

    @classmethod
    def from_attacks(cls, attacks, dist=None):
        """
        Construct a Fliptable from a sequence of Attack objects.

        dist, if specified and not None, overrides automatic target distance detection.
        """
        ranges = []
        hammers = []
        flips = []
        tmpatk = []

        last = DRAMAddr(-1,-1,-1,-1,-1,-1)
        for atk in attacks:
            if dist is None:
                dist = (atk.targets[1] - atk.targets[0]).row
            if (atk.targets[1] - atk.targets[0]).row != dist:
                raise ValueError('Inconsistent target distance: {}, expected {}'.format(atk.targets, dist))

            if atk.targets[0] - last == DRAMAddr(row=1):
                tmpatk.append(atk)
            else:
                if tmpatk:
                    ranges.append(Range(tmpatk[0].targets[0], len(tmpatk), len(hammers)))
                    for a in tmpatk:
                        hammers.append(Hammering(len(a.victims), len(flips)))
                        flips.extend(a.victims)
                tmpatk = [atk]
            last = atk.targets[0]
        if tmpatk:
            ranges.append(Range(tmpatk[0].targets[0], len(tmpatk), len(hammers)))
            for a in tmpatk:
                hammers.append(Hammering(len(a.victims), len(flips)))
                flips.extend(a.victims)

        ranges.sort(key=lambda r: r.start.numeric_value)

        return cls(dist, ranges, hammers, flips)
