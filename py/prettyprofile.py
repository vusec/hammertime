#!/usr/bin/env python3
# -*- coding: UTF-8 -*-
# Copyright (c) 2016 Andrei Tatar
# Copyright (c) 2018 Vrije Universiteit Amsterdam
#
# This program is licensed under the GPL2+.


import sys

from hammertime import fliptable


def prettify_profile_line(line):
    atk = fliptable.Attack.decode_line(line)
    head = ('Hammering row{} '.format('s' if len(atk.targets) > 1 else '') +
            ', '.join(str(x.row) for x in atk.targets) +
            ' on bank {0.bank}, rank {0.rank}, DIMM {0.dimm}, channel {0.chan}\n'.format(atk.targets[0])
            )
    tail = '\t' + '\n\t'.join(
        'Bit flip on row {:6}, column {:4}, bit {:2}: {}'.format(
            x.addr.row, x.addr.col, x.bit, '0 -> 1' if x.pullup else '1 -> 0'
        ) for x in atk)
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
