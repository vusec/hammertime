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

"""Module providing utilities for working with profile output and fliptables."""

import re
import functools

from collections import namedtuple
from collections import OrderedDict

from hammertime import fliptable
from pyramses import DRAMAddr

ROW_FMT = r'\((\w+)\s+(\w+)\s+(\w+)\s+(\w+)\s+(\w+)\s*(\w+)?\)'
ROW_RE = re.compile(ROW_FMT)
BFLIP_FMT = r'(\w{4})\|(\w{2})\|(\w{2})'
BFLIP_RE = re.compile(BFLIP_FMT)

VICT_FMT = r'{} (?:{}\s?)+'.format(ROW_FMT, BFLIP_FMT)
VICT_RE = re.compile(VICT_FMT)

class BitFlip(namedtuple('BitFlip', ['off', 'got', 'exp'])):
    def __str__(self):
        return '{0.off:04x}|{0.got:02x}|{0.exp:02x}'.format(self)


class HamRun(namedtuple('HamRun', ['targets', 'victims'])):
    def __str__(self):
        tstr = ' '.join(str(x) for x in self.targets) + ' : '
        vstr = ' '.join(' '.join(map(str, x)) for x in self.victims)
        return tstr + vstr


def decode_line(line):
    targ, vict, *o = line.split(':')
    victs = []
    for x in VICT_RE.finditer(vict):
        vrow = DRAMAddr(*(int(v, 16) for v in x.group(*range(1,7)) if v is not None))
        vflips = [BitFlip(*(int(v, 16) for v in y.groups())) for y in BFLIP_RE.finditer(x.group(0))]
        victs.append((vrow, vflips))
    return HamRun(
        targets = [DRAMAddr(*(int(v, 16) for v in x.groups() if v is not None)) for x in ROW_RE.finditer(targ)],
        victims = victs
    )


def _attack2hamruns(attack, vict_exp):
    v1 = []
    v2 = []
    for f in attack.victims:
        g1 = (vict_exp | (f.pullup & ~vict_exp)) & ~(f.pulldown & vict_exp) & 0xff
        g2 = ((~vict_exp & 0xff) | (f.pullup & vict_exp)) & ~(f.pulldown & ~vict_exp) & 0xff
        if g1 != vict_exp:
            v1.append((f.addr, BitFlip(f.cell_byte, g1, vict_exp)))
        if g2 != (~vict_exp & 0xff):
            v2.append((f.addr, BitFlip(f.cell_byte, g2, ~vict_exp & 0xff)))

    return (HamRun(attack.targets, v1), HamRun(attack.targets, v2))


def fliptable2profile(ftbl, vict_exp=0xff):
    return '\n'.join('\n'.join(str(ham) for ham in _attack2hamruns(atk, vict_exp)) for atk in ftbl)


def _allequal(sequence):
    """Return True if all non-None elements in sequence are equal, False otherwise"""
    return functools.reduce(lambda a, b: a if a == b else None, sequence) is not None


def _hamruns2attack(hams):
    flips = OrderedDict()
    targets = []
    for ham in hams:
        targets.append(ham.targets)
        for vrow, bflips in ham.victims:
            for x in bflips:
                flip_addr = vrow + DRAMAddr(col=x.off // 8)
                cbyte = x.off % 8
                key = (flip_addr, cbyte)
                try:
                    prevf = flips[key]
                    flips[key] = fliptable.Flip(
                        addr=flip_addr,
                        cell_byte=cbyte,
                        pullup=(~x.exp & x.got & 0xff) | prevf.pullup,
                        pulldown=(x.exp & ~x.got & 0xff) | prevf.pulldown
                    )
                except KeyError:
                    flips[key] = fliptable.Flip(
                        addr=flip_addr,
                        cell_byte=cbyte,
                        pullup=(~x.exp & x.got & 0xff),
                        pulldown=(x.exp & ~x.got & 0xff)
                    )
    if not _allequal(targets):
        raise ValueError('Inconsistent targets among HamRuns')
    t0 = targets[0]
    if len(t0) == 1:
        t0 *= 2
    elif len(t0) != 2:
        raise ValueError('Unsupported hammering pattern')
    return fliptable.Attack(t0, list(flips.values()))


def profile2attacks(prof_path):
    with open(prof_path, 'r') as f:
        last = None
        hams = []
        for ham in (decode_line(l) for l in f if not l.lstrip().startswith('#')):
            if ham.targets != last and hams:
                yield _hamruns2attack(hams)
                hams.clear()
            hams.append(ham)
            last = ham.targets
    if hams:
        yield _hamruns2attack(hams)


def profile2fliptable(prof_path):
    return fliptable.Fliptable.from_attacks(profile2attacks(prof_path))
