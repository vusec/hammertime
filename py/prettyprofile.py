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

import sys

import hammerprof


def prettify_profile_line(line):
    run = hammerprof.decode_line(line)
    head = ('Hammering row{} '.format('s' if len(run.targets) > 1 else '') +
            ', '.join(str(x.row) for x in run.targets) +
            ' on bank {0.bank}, rank {0.rank}, DIMM {0.dimm}, channel {0.chan}\n'.format(run.targets[0])
            )
    tail = '\t' + '\n\t'.join(
        'Bit flip on row {:6} byte {:4}: expected {} got {}'.format(
            k.row, x.off, bin(x.exp)[2:].zfill(8), bin(x.got)[2:].zfill(8)
        ) for k, v in run.victims.items() for x in v)
    return head + tail


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print('usage: {} PROFILE_PATH'.format(sys.argv[0]))
    else:
        if sys.argv[1] == '-':
            f = sys.stdin
        else:
            f = open(sys.argv[1], 'r')
        for l in f:
            print(prettify_profile_line(l))
        f.close()
