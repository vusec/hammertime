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

import os
import sys
import ctypes
import ctypes.util

LIBNAME = 'libramses.so'

class RamsesError(Exception):
    """Exception class used to encapsulate ramses errors"""


class DRAMAddr(ctypes.Structure):
    _fields_ = [('chan', ctypes.c_ubyte),
                ('dimm', ctypes.c_ubyte),
                ('rank', ctypes.c_ubyte),
                ('bank', ctypes.c_ubyte),
                ('row', ctypes.c_ushort),
                ('col', ctypes.c_ushort)]

    def __str__(self):
        return '({0.chan:1x} {0.dimm:1x} {0.rank:1x} {0.bank:1x} {0.row:4x} {0.col:3x})'.format(self)

    def __repr__(self):
        return '{0}({1.chan}, {1.dimm}, {1.rank}, {1.bank}, {1.row}, {1.col})'.format(type(self).__name__, self)

    def __eq__(self, other):
        return self.same_bank(other) and self.row == other.row and self.col == other.col

    def __hash__(self):
        return (self.col + self.row << 16 + self.bank << 32 +
                self.rank << 40 + self.dimm << 48 + self.chan << 52)

    def __len__(self):
        return len(self._fields_)

    def __getitem__(self, key):
        if isinstance(key, int):
            return getattr(self, self._fields_[key][0])
        elif isinstance(key, slice):
            start = key.start if key.start is not None else 0
            stop = key.stop if key.stop is not None else len(self._fields_)
            step = key.step if key.step is not None else 1
            return tuple(getattr(self, self._fields_[k][0]) for k in range(start, stop, step))
        else:
            raise TypeError('{} object cannot be indexed by {}'.format(type(self).__name__, type(key).__name__))

    def same_bank(self, other):
        return (self.chan == other.chan and self.dimm == other.dimm and
                self.rank == other.rank and self.bank == other.bank)

    def add_offset(self, off):
        return type(self)(self.chan, self.dimm, self.rank, self.bank, self.row, self.col + off//8)

    def add_row(self, roff):
        return type(self)(self.chan, self.dimm, self.rank, self.bank, self.row + roff, self.col)


class MemorySystem(ctypes.Structure):
    _fields_ = [('router', ctypes.c_uint),
                ('controller', ctypes.c_uint),
                ('dimm_remap', ctypes.c_uint),
                ('mem_geometry', ctypes.c_int),
                ('route_opts', ctypes.c_void_p),
                ('controller_opts', ctypes.c_void_p)]

    @staticmethod
    def _assert_dlls():
        if _lib is None or _libc is None:
            init_dlls()

    def load_file(self, fname):
        self._assert_dlls()
        cfn = ctypes.c_char_p(fname.encode('utf-8'))
        f = _libc.fopen(cfn, b'r')
        if not f:
            raise OSError('fopen call failed: {}'.format(os.strerror(ctypes.c_int.in_dll(_libc, 'errno').value)))
        r = _lib.ramses_memsys_load_file(f, ctypes.byref(self), None)
        _libc.fclose(f)
        if r:
            raise RamsesError('ramses_memsys_load_file returned non-zero: {}'.format(r))

    def load_str(self, s):
        self._assert_dlls()
        cs = ctypes.c_char_p(s.encode('utf-8'))
        r = _lib.ramses_memsys_load_str(cs, len(s), ctypes.byref(self), None)
        if r:
            raise RamsesError('ramses_memsys_load_str returned non-zero: {}'.format(r))

    @property
    def granularity(self):
        self._assert_dlls()
        return _lib.ramses_map_granularity(self.controller, self.mem_geometry, self.controller_opts)

    def resolve(self, phys_addr):
        self._assert_dlls()
        cpa = ctypes.c_ulonglong(phys_addr)
        return _lib.ramses_resolve(ctypes.byref(self), cpa)

    def resolve_reverse(self, dram_addr):
        self._assert_dlls()
        return _lib.ramses_resolve_reverse(ctypes.byref(self), dram_addr)

    def route(self, phys_addr):
        self._assert_dlls()
        return _lib.ramses_route(self.router, phys_addr, self.route_opts)

    def route_reverse(self, mem_addr):
        self._assert_dlls()
        return _lib.ramses_route_reverse(self.router, mem_addr, self.route_opts)

    def map(self, mem_addr):
        self._assert_dlls()
        return _lib.ramses_map(self.controller, mem_addr, self.mem_geometry, self.controller_opts)

    def map_reverse(self, dram_addr):
        self._assert_dlls()
        return _lib.ramses_map_reverse(self.controller, dram_addr, self.mem_geometry, self.controller_opts)

    def remap(self, dram_addr):
        self._assert_dlls()
        return _lib.ramses_remap(self.dimm_remap, dram_addr)

    def remap_reverse(self, dram_addr):
        self._assert_dlls()
        return _lib.ramses_remap_reverse(self.dimm_remap, dram_addr)

    def __del__(self):
        try:
            if _lib is not None:
                _lib.ramses_memsys_free(ctypes.byref(self))
        except NameError:
            pass


# Module init code

try:
    _MODULE_DIR = os.path.abspath(os.path.dirname(sys.modules[__name__].__file__))
except AttributeError:
    _MODULE_DIR = os.getcwd()


_SEARCH_PATHS = [os.path.abspath(os.path.join(_MODULE_DIR, x)) for x in
    ['.', '..']
]

_lib = None
_libc = None


def init_dlls(extra_paths=None):
    global _libc
    global _lib
    global _SEARCH_PATHS

    if extra_paths is not None:
        _SEARCH_PATHS = list(extra_paths) + _SEARCH_PATHS
    _libc = ctypes.cdll.LoadLibrary(ctypes.util.find_library('c'))
    for p in _SEARCH_PATHS:
        try:
            _lib = ctypes.cdll.LoadLibrary(os.path.join(p, LIBNAME))
            break
        except OSError:
            pass
    else:
        _lib = ctypes.cdll.LoadLibrary(LIBNAME)

    _lib.ramses_memsys_load_file.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
    _lib.ramses_memsys_load_str.argtypes = [ctypes.c_char_p, ctypes.c_size_t,
                                            ctypes.c_void_p, ctypes.c_void_p]
    _lib.ramses_resolve.restype = DRAMAddr
    _lib.ramses_resolve.argtypes = [ctypes.c_void_p, ctypes.c_ulonglong]
    _lib.ramses_resolve_reverse.restype = ctypes.c_ulonglong
    _lib.ramses_resolve_reverse.argtypes = [ctypes.c_void_p, ctypes.c_ulonglong]

    _lib.ramses_route.restype = ctypes.c_ulonglong
    _lib.ramses_route.argtypes = [ctypes.c_uint, ctypes.c_ulonglong, ctypes.c_void_p]
    _lib.ramses_route_reverse.restype = ctypes.c_ulonglong
    _lib.ramses_route_reverse.argtypes = [ctypes.c_uint, ctypes.c_ulonglong, ctypes.c_void_p]

    _lib.ramses_map.restype = DRAMAddr
    _lib.ramses_map.argtypes = [ctypes.c_uint, ctypes.c_ulonglong,
                                ctypes.c_int, ctypes.c_void_p]
    _lib.ramses_map_reverse.restype = ctypes.c_ulonglong
    _lib.ramses_map_reverse.argtypes = [ctypes.c_uint, DRAMAddr,
                                        ctypes.c_int, ctypes.c_void_p]

    _lib.ramses_remap.restype = DRAMAddr
    _lib.ramses_remap.argtypes = [ctypes.c_uint, DRAMAddr]
    _lib.ramses_remap_reverse.restype = DRAMAddr
    _lib.ramses_remap_reverse.argtypes = [ctypes.c_uint, DRAMAddr]

    _lib.ramses_map_granularity.restype = ctypes.c_ulonglong
    _lib.ramses_map_granularity.argtypes = [ctypes.c_uint, ctypes.c_int, ctypes.c_void_p]

    _libc.fopen.restype = ctypes.c_void_p
    _libc.fopen.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
    _libc.fclose.argtypes = [ctypes.c_void_p]

# End module init code
