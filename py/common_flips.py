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
import argparse

from hammertime import profile


def _comm_reduce(pfiles):
    ft = profile.profile2fliptable(pfiles[0])
    for pfile in pfiles[1:]:
        ft = ft.diff(profile.profile2fliptable(pfile))[1]
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
    print(profile.fliptable2profile(_comm_reduce(args.profiles)), file=outf)
    outf.close()
