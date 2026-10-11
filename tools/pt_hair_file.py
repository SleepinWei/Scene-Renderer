"""Strict little-endian CY HAIR reader. Keeps original point/attribute arrays."""
from array import array
from dataclasses import dataclass
import math
from pathlib import Path
import struct
import sys


@dataclass
class HairFile:
    strands: int
    points: array
    offsets: array
    thickness: array
    transparency: array
    colors: array
    default_thickness: float
    default_transparency: float
    default_color: tuple
    info: str
    flags: int

    def strand(self, index):
        start, end = self.offsets[index:index+2]
        return [list(self.points[3*i:3*i+3]) for i in range(start, end)]


def read_hair(path):
    with Path(path).open('rb') as file:
        header = file.read(128)
        if len(header) != 128:
            raise ValueError('Truncated HAIR header')
        magic, ns, np, flags, default_segments, width, opacity, r, g, b, info = struct.unpack('<4sIIIIff3f88s', header)
        if magic != b'HAIR' or flags & ~31 or not flags & 2:
            raise ValueError('Invalid HAIR signature or flags')
        if not 1 <= ns <= 1000000 or not 2*ns <= np <= 20000000:
            raise ValueError('HAIR counts outside supported budget')
        if not all(math.isfinite(v) for v in (width, opacity, r, g, b)) or width < 0 or not 0 <= opacity <= 1 or min(r, g, b) < 0:
            raise ValueError('Invalid HAIR default attributes')
        def values(code, count):
            result = array(code)
            size = count*result.itemsize
            data = file.read(size)
            if len(data) != size:
                raise ValueError('Truncated HAIR array')
            result.frombytes(data)
            if sys.byteorder != 'little':
                result.byteswap()
            return result
        segments = values('H', ns) if flags & 1 else None
        if segments is None and not 1 <= default_segments <= 65535:
            raise ValueError('Invalid default segment count')
        offsets = array('I', [0])
        for i in range(ns):
            n = segments[i] if segments is not None else default_segments
            if n < 1 or offsets[-1]+n+1 > np:
                raise ValueError('HAIR segment/point counts disagree')
            offsets.append(offsets[-1]+n+1)
        if offsets[-1] != np:
            raise ValueError('HAIR segment/point counts disagree')
        points = values('f', np*3)
        thickness = values('f', np) if flags & 4 else array('f')
        transparency = values('f', np) if flags & 8 else array('f')
        colors = values('f', np*3) if flags & 16 else array('f')
        if file.read(1):
            raise ValueError('Unexpected trailing HAIR data')
        if not all(math.isfinite(v) for a in (points, thickness, transparency, colors) for v in a):
            raise ValueError('Nonfinite HAIR attribute')
        if (thickness and (min(thickness) < 0 or max(thickness) <= 0)) or (not thickness and width <= 0):
            raise ValueError('Invalid HAIR thickness')
        if transparency and (min(transparency) < 0 or max(transparency) > 1):
            raise ValueError('Invalid HAIR transparency')
        if colors and min(colors) < 0:
            raise ValueError('Invalid HAIR color')
        return HairFile(ns, points, offsets, thickness, transparency, colors, width, opacity,
                        (r, g, b), info.split(b'\0', 1)[0].decode('ascii', errors='replace'), flags)
