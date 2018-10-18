#!/usr/bin/env python3
# -*- coding: UTF-8 -*-
# Copyright (c) 2016 Andrei Tatar
# Copyright (c) 2018 Vrije Universiteit Amsterdam
#
# This program is licensed under the GPL2+.


import sys

from hammertime import fliptable


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Missing arguments")
        print("usage: {} PROFILE_PATH [...]".format(sys.argv[0]))
    else:
        try:
            for fn in sys.argv[1:]:
                print('Stats for {}:'.format(fn))
                ft = fliptable.Fliptable.load_file(fn)
                natks = str(len(ft))
                print('Hammers: {}'.format(natks))
                flips = [len(x.flips) for x in ft if x.flips]
                print('w/flips: {{:{}d}}'.format(len(natks)).format(len(flips)))
                print('Total Bit Flips: {}'.format(sum(flips)))
        except KeyboardInterrupt:
            print('Interrupted, exiting...')
