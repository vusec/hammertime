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

import hammertime.profile


if __name__ == '__main__':
    if len(sys.argv) < 3:
        print("Missing arguments")
        print("usage: {} PROFILE_PATH FLIPTABLE_PATH".format(sys.argv[0]))
    else:
        try:
            ft = hammertime.profile.profile2fliptable(sys.argv[1])
            print('Ranges: {}'.format(len(ft.ranges)))
            print('Hammers: {}'.format(len(ft.hammers)))
            print('Flips: {}'.format(len(ft.flips)))
            ft.write(sys.argv[2])
        except KeyboardInterrupt:
            print('Interrupted, exiting...')
