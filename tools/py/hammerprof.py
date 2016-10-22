#!/usr/bin/env python3
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

"""Module providing utilities for working with profile output and fliptables."""

import re
import sys
import math
import struct

from collections import namedtuple
from collections import OrderedDict

ROW_FMT = r'\((\w+)\s+(\w+)\s+(\w+)\s+(\w+)\s+(\w+)\)'
ROW_RE = re.compile(ROW_FMT)
BFLIP_FMT = r'(\w{4})\|(\w{2})\|(\w{2})'
BFLIP_RE = re.compile(BFLIP_FMT)

VICT_FMT = r'{} (?:{} )+'.format(ROW_FMT, BFLIP_FMT)
VICT_RE = re.compile(VICT_FMT)

DRAMAddr = namedtuple('DRAMAddr', ['chan', 'dimm', 'rank', 'bank', 'row', 'col'])
DRAMAddr.same_bank = lambda s, x: (s.chan == x.chan and s.dimm == x.dimm and
                                   s.rank == x.rank and s.bank == x.bank)
DRAMAddr.add_offset = lambda s, off: DRAMAddr(s.chan, s.dimm, s.rank, s.bank, s.row, s.col + off//8)

BitFlip = namedtuple('BitFlip', ['off', 'got', 'exp'])
HamRun = namedtuple('HamRun', ['targets', 'victims'])

FTFlip = struct.Struct('BBBBHH HBB')
FTHammering = struct.Struct('II')
FTRange = struct.Struct('BBBBHH II')
FTHeader = struct.Struct('IIQQQQIII')

FLIPTBL_FILE_MAGIC = 0xf11b7ab1
FLIPTBL_FILE_ALIGN = 0x80

Flip = namedtuple('Flip', ['addr', 'cell_byte', 'pullup', 'pulldown'])
Hammering = namedtuple('Hammering', ['nflips', 'idx'])
Range = namedtuple('Range', ['start', 'nhammers', 'idx'])
Range.update_counts = lambda s, x: Range(start=s.start, nhammers=x - s.idx, idx=s.idx)


def decode_line(line):
    targ, vict, *o = line.split(':')
    victs = OrderedDict()
    for x in VICT_RE.finditer(vict):
        vrow = DRAMAddr(*(int(v, 16) for v in x.group(*range(1,6))), col=0)
        vflips = [BitFlip(*(int(v, 16) for v in y.groups())) for y in BFLIP_RE.finditer(x.group(0))]
        if vrow in victs:
            victs[vrow].extend(vflips)
        else:
            victs[vrow] = vflips
    return HamRun(
        targets = [DRAMAddr(*(int(v, 16) for v in x.groups()), col=0) for x in ROW_RE.finditer(targ)],
        victims = victs
    )


def _align(n):
    return math.ceil(n / FLIPTBL_FILE_ALIGN) * FLIPTBL_FILE_ALIGN

def _rangekey(r):
    a = r.start
    return (a.col +
            a.row << 16 +
            a.bank << 32 +
            a.rank << 40 +
            a.dimm << 48 +
            a.chan << 52)

def profile2fliptable(prof_path, out_path):
    ranges = []
    hammers = []
    flips = []
    curflips = OrderedDict()

    with open(prof_path, 'r') as f:
        r0 = decode_line(f.readline())
        if len(r0.targets) == 1:
            dist = 0
        elif len(r0.targets) == 2:
            dist = r0.targets[1].row - r0.targets[0].row
        else:
            raise ValueError("Unsupported hammering pattern")

        last = r0.targets[0]
        ranges.append(Range(start=last, nhammers=1, idx=len(hammers)))

        f.seek(0)
        for run in (decode_line(l) for l in f):
            t = run.targets[0]
            if t != last:
                hammers.append(Hammering(nflips=len(curflips), idx=len(flips)))
                flips.extend(curflips.values())
                curflips.clear()
            for vrow, bflips in run.victims.items():
                for x in bflips:
                    flip_addr = vrow.add_offset(x.off)
                    cbyte = x.off % 8
                    key = (flip_addr, cbyte)
                    try:
                        prevf = curflips[key]
                        curflips[key] = Flip(
                            addr=flip_addr,
                            cell_byte=cbyte,
                            pullup=(~x.exp & x.got & 0xff) | prevf.pullup,
                            pulldown=(x.exp & ~x.got & 0xff) | prevf.pulldown
                        )
                    except KeyError:
                        curflips[key] = Flip(
                            addr=flip_addr,
                            cell_byte=cbyte,
                            pullup=(~x.exp & x.got & 0xff),
                            pulldown=(x.exp & ~x.got & 0xff)
                        )

            if t != last and not (t.same_bank(last) and t.row == last.row + 1):
                ranges[-1] = ranges[-1].update_counts(len(hammers))
                ranges.append(Range(start=t, nhammers=1, idx=len(hammers)))
            last = t
    hammers.append(Hammering(nflips=len(curflips), idx=len(flips)))
    flips.extend(curflips.values())
    curflips.clear()
    ranges[-1] = ranges[-1].update_counts(len(hammers))

    ranges.sort(key=_rangekey)

    rangesz = len(ranges) * FTRange.size
    hammersz = len(hammers) * FTHammering.size
    flipsz = len(flips) * FTFlip.size
    rangeoff = _align(FTHeader.size)
    hammeroff = _align(rangeoff + rangesz)
    flipoff = _align(hammeroff + hammersz)
    size = flipoff + flipsz
    print("Ranges:", len(ranges))
    print("Hammers:", len(hammers))
    print("Flips:", len(flips))

    with open(out_path, 'wb') as f:
        f.write(FTHeader.pack(FLIPTBL_FILE_MAGIC, dist, size, rangeoff, hammeroff,
                              flipoff, len(ranges), len(hammers), len(flips)))
        f.seek(rangeoff)
        f.write(b''.join(FTRange.pack(*(x.start + x[1:])) for x in ranges))
        f.seek(hammeroff)
        f.write(b''.join(FTHammering.pack(*x) for x in hammers))
        f.seek(flipoff)
        f.write(b''.join(FTFlip.pack(*(x.addr + x[1:])) for x in flips))


if __name__ == '__main__':
    if len(sys.argv) < 3:
        print("Missing arguments")
        print("usage: {} PROFILE_PATH FLIPTABLE_PATH".format(sys.argv[0]))
    else:
        profile2fliptable(sys.argv[1], sys.argv[2])
