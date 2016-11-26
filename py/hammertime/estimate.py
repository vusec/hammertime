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

import math
import itertools
from collections import namedtuple

import pyramses
from hammertime import profile

PageBitFlip = namedtuple('PageBitFlip', ['page_offset', 'mask'])

class BitPullup(PageBitFlip):
    """Represents 0->1 flip(s) in one byte at a particular offset in a page"""
class BitPulldown(PageBitFlip):
    """Represents 1->0 flip(s) in one byte at a particular offset in a page"""

class VictimPage(namedtuple('VictimPage', ['pfn', 'pullups', 'pulldowns'])):
    """Represents the results of one rowhammer attack on one particular physical page"""


class ExploitModel:

    def check_page(self, vpage):
        raise NotImplementedError()

    def check_attack(self, attack):
        for vpage in attack:
            if self.check_page(vpage):
                yield vpage.pfn

    def check_attacks(self, attacks):
        for atk in attacks:
            yield tuple(self.check_attack(atk))


def _map_attack(atk, memsys):
    mapped_flips = {}
    flip_addrs = []
    vict_pages = []
    for flip in atk.victims:
        pa = memsys.resolve_reverse(flip.addr)
        mapped_flips[pa] = flip
        flip_addrs.append(pa)
    flip_addrs.sort()
    page_start = 0
    ups = []
    downs = []

    for fa in flip_addrs:
        if fa > page_start + 0x1000:
            vict_pages.append(VictimPage(pfn=page_start >> 12, pullups=set(ups), pulldowns=set(downs)))
            ups.clear()
            downs.clear()
            page_start = fa >> 12 << 12
        f = mapped_flips[fa]
        off = (fa % 0x1000) + f.cell_byte
        if f.pullup:
            ups.append(BitPullup(page_offset=off, mask=f.pullup))
        if f.pulldown:
            downs.append(BitPulldown(page_offset=off, mask=f.pulldown))
    if ups or downs:
        vict_pages.append(VictimPage(pfn=page_start >> 12, pullups=set(ups), pulldowns=set(downs)))

    return vict_pages


class BaseEstimator:
    # Default and fallback in case exploit not run
    results = []

    def iter_attacks(self):
        """
        Return an iterator over possible attacks

        An attack consists of a sequence of VictimPages that have bits flipped
        as part of a single Rowhammer attack.
        """
        raise NotImplementedError()

    def run_exploit(self, model):
        self.results = list(model.check_attacks(self.iter_attacks()))

    def print_stats(self):
        if self.results:
            succ = sum(1 for x in self.results if x)
            npages = sum(len(x) for x in self.results if x)
            prop = succ / len(self.results)
            print('{} total attacks (over {} KiB), of which {} successful ({:5.1f} %)'.format(
                len(self.results), len(self.results) * 8, succ, 100.0 * prop
            ))
            print('{} exploitable pages found'.format(npages))
            if prop != 0:
                mna = 1 / prop
                print('Minimum (contiguous) memory required: {} KiB'.format(math.ceil(mna) * 8))
                print('Mean number of attacks until successful: {:.1f}'.format(mna))
                print('Mean time to successful attack: {:.1f} seconds (assuming 200ms/attack)'.format(mna * 0.2))



class FliptableEstimator(BaseEstimator):

    def __init__(self, fliptbl, memsys):
        self.fliptbl = fliptbl
        self.msys = memsys

    def iter_attacks(self):
        for atk in self.fliptbl:
            yield _map_attack(atk, self.msys)

    @classmethod
    def main(cls, profile_file, msys_file, model):
        """Set up an estimator, run an exploit and print out statistics"""
        ftbl = profile.profile2fliptable(profile_file)
        msys = pyramses.MemorySystem()
        msys.load_file(msys_file)
        est = cls(ftbl, msys)
        est.run_exploit(model)
        est.print_stats()
