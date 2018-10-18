#!/usr/bin/env python3
# -*- coding: UTF-8 -*-
# Copyright (c) 2016 Andrei Tatar
# Copyright (c) 2018 Vrije Universiteit Amsterdam
#
# This program is licensed under the GPL2+.


import sys
import argparse

from hammertime import fliptable


def _comm_reduce(pfiles):
    ft = fliptable.Fliptable.load_file(pfiles[0])
    for pfile in pfiles[1:]:
        ft = ft.diff(fliptable.Fliptable.load_file(pfile)).common
    return ft

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Find common bit flips among multiple profile results')
    parser.add_argument('profiles', metavar='PROFILE', nargs='+',
                        help='Profile result file')
    parser.add_argument('-o', '--output', action='store', default='-',
                        help='Specify output file; by default will output to stdout')
    args = parser.parse_args()

    if args.output == '-':
        outf = sys.stdout
    else:
        outf = open(args.output, 'w')
    print(_comm_reduce(args.profiles), file=outf)
    outf.close()
