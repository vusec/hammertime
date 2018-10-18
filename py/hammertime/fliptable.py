#!/usr/bin/env python3
# -*- coding: UTF-8 -*-
# Copyright (c) 2016 Andrei Tatar
# Copyright (c) 2017-2018 Vrije Universiteit Amsterdam
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

from collections import namedtuple

from pyramses import DRAMAddr


ADDR_FMT = r'\((\w+)\s+(\w+)\s+(\w+)\s+(\w+)\s+(\w+)\s*(\w+)?\)'
ADDR_RE = re.compile(ADDR_FMT)
BFLIP_FMT = r'(\w{4})\|(\w{2})\|(\w{2})'
BFLIP_RE = re.compile(BFLIP_FMT)

VICT_FMT = r'{} (?:{}\s?)+'.format(ADDR_FMT, BFLIP_FMT)
VICT_RE = re.compile(VICT_FMT)


class Corruption(namedtuple('Corruption', ['off', 'got', 'exp'])):
    def __str__(self):
        return '{0.off:04x}|{0.got:02x}|{0.exp:02x}'.format(self)

    def to_flips(self, base, msys=None):
        if msys is None:
            csize = 8 # Sane default for memory cell size
        else:
            csize = msys.mapping.props.cell_size
        addr = base + DRAMAddr(col=self.off // csize)
        byte = self.off % csize
        flips = []
        pup = ~self.exp & self.got & 0xff
        pdn = self.exp & ~self.got & 0xff
        bit = 0
        while pup:
            mask = 1 << bit
            if (pup & mask):
                flips.append(Flip(addr, 8*byte + bit, True))
                pup &= ~mask
            bit += 1
        bit = 0
        while pdn:
            mask = 1 << bit
            if (pdn & mask):
                flips.append(Flip(addr, 8*byte + bit, False))
                pdn &= ~mask
            bit += 1
        return flips


class Flip(namedtuple('Flip', ['addr', 'bit', 'pullup'])):
    def to_corruption(self, pattern=None):
        byte = self.bit // 8
        fmask = 1 << (self.bit % 8)
        if pattern is None:
            pat = 0 if self.pullup else 0xff
        else:
            pat = pattern[byte % len(pattern)]
        val = pat | fmask if self.pullup else pat & ~fmask
        return Corruption(off=byte, got=val, exp=pat)

    def to_physmem(self, msys):
        return type(self)(msys.resolve_reverse(self.addr), self.bit, self.pullup)


Diff = namedtuple('Diff', ['self_only', 'common', 'other_only'])


class Attack(namedtuple('Attack', ['targets', 'flips'])):
    def diff(self, other):
        if not isinstance(other, type(self)):
            raise TypeError('Attack instance expected for diff')
        elif not self.targets == other.targets:
            raise ValueError('Cannot diff attacks with different targets')
        else:
            return Diff(
                type(self)(self.targets, self.flips - other.flips),
                type(self)(self.targets, self.flips & other.flips),
                type(self)(self.targets, other.flips - self.flips)
            )

    def merge(self, other):
        if not isinstance(other, type(self)):
            raise TypeError('Attack instance expected for merge')
        elif not self.targets == other.targets:
            raise ValueError('Cannot merge attacks with different targets')
        else:
            return type(self)(self.targets, self.flips | other.flips)

    def to_corruptions(self, pat=None):
        return ((x.addr, x.to_corruption(pat)) for x in self)

    def to_physmem(self, msys):
        return type(self)(
            targets=tuple(msys.resolve_reverse(x) for x in self.targets),
            flips={x.to_physmem(msys) for x in self.flips}
        )

    def __iter__(self):
        return iter(sorted(self.flips))

    @classmethod
    def decode_line(cls, line, msys=None):
        targ, vict, *o = line.split(':')
        flips = set()
        for x in VICT_RE.finditer(vict):
            vaddr = DRAMAddr(*(int(v, 16) for v in x.group(*range(1,7)) if v is not None))
            vcorr = [Corruption(*(int(v, 16) for v in y.groups())) for y in BFLIP_RE.finditer(x.group(0))]
            for corr in vcorr:
                flips.update(corr.to_flips(vaddr, msys))
        return cls(
            targets=[DRAMAddr(*(int(v, 16) for v in x.groups() if v is not None)) for x in ADDR_RE.finditer(targ)],
            flips=flips
        )

    def encode(self, patterns=None):
        if patterns is None:
            patterns = [[0xff], [0]]
        corrs = [
            [x for x in self.to_corruptions(pat) if x[1].exp != x[1].got]
            for pat in patterns
        ]
        tstr = ' '.join(str(x) for x in self.targets) + ' : '
        return '\n'.join(
            tstr +
            ' '.join(' '.join(str(x) for x in c) for c in corr)
            for corr in corrs
        )


def decode_lines(lineiter):
    curatk = None
    for line in lineiter:
        atk = Attack.decode_line(line)
        if curatk is None:
            curatk = atk
        else:
            try:
                curatk = curatk.merge(atk)
            except ValueError:
                yield curatk
                curatk = atk
    if curatk is not None:
        yield curatk


class Fliptable:
    def __init__(self, attacks):
        self.attacks=attacks

    def __eq__(self, other):
        if isinstance(other, type(self)):
            return self.attacks == other.attacks
        else:
            return NotImplemented

    def __len__(self):
        return len(self.attacks)

    def __iter__(self):
        return iter(self.attacks)

    def __str__(self):
        return '\n'.join(atk.encode() for atk in self)

    def diff(self, other):
        if not isinstance(other, type(self)):
            raise ValueError('Fliptable required for diff')
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
                uother.extend(oatks)
                break
            if oa is None:
                uself.append(sa)
                uself.extend(satks)
                break
            try:
                adiff = sa.diff(oa)
                if adiff.self_only.flips:
                    uself.append(adiff.self_only)
                common.append(adiff.common)
                if adiff.other_only.flips:
                    uother.append(adiff.other_only)
                sa = next(satks, None)
                oa = next(oatks, None)
            except ValueError:
                if sa.targets < oa.targets:
                    uself.append(sa)
                    sa = next(satks, None)
                elif sa.targets > oa.targets:
                    uother.append(oa)
                    oa = next(oatks, None)
        FT = type(self)
        return Diff(FT(uself), FT(common), FT(uother))

    def to_physmem(self, msys):
        return type(self)([x.to_physmem(msys) for x in self])

    @classmethod
    def load_file(cls, fname):
        with open(fname, 'r') as f:
            return cls(list(decode_lines(f)))

    def save_file(self, fname):
        with open(fname, 'w') as f:
            f.write(str(self))
