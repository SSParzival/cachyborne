# SPDX-License-Identifier: GPL-3.0-or-later
"""Generated world assets for BBHOST features, without editing the dumped game.

The mirror layout and runtime preview controller are prepared together by the
launcher. Generated files never modify the original dump.
"""
from bisect import bisect_right
import hashlib
import os
from pathlib import Path
import struct
import tempfile
import zlib

MIRROR_PATH = Path('dvdroot_ps4/map/mapstudio/m21_00_00_00.msb.dcx')
MIRROR_INPUT = '8e1de0ee223cd5137a93383cfc7bb1d27964bc480c63e35013674717b403ef8f'
MIRROR_OUTPUT = '99d5c7b7f52307344c6e8296b499d5e57969f21c443fcc607cb42cb2c38c8afa'
MAX_LAYOUT = 16 << 20


def unpack_layout(dcx, *, max_size=MAX_LAYOUT):
    """Strict bounded DFLT DCX decoding, including size and stream boundaries."""
    if (len(dcx) < 0x4c or dcx[:4] != b'DCX\0' or dcx[0x18:0x1c] != b'DCS\0'
            or dcx[0x24:0x2c] != b'DCP\0DFLT' or dcx[0x44:0x48] != b'DCA\0'):
        raise ValueError('Unsupported world-layout DCX header')
    size, packed = struct.unpack_from('>II', dcx, 0x1c)
    if not 0 < size <= max_size or packed != len(dcx)-0x4c:
        raise ValueError('Invalid world-layout DCX sizes')
    decoder = zlib.decompressobj()
    try:
        data = decoder.decompress(dcx[0x4c:], size+1)
    except zlib.error as error:
        raise ValueError('Invalid world-layout DCX stream') from error
    if len(data) != size or not decoder.eof or decoder.unused_data or decoder.unconsumed_tail:
        raise ValueError('Incomplete or oversized world-layout DCX stream')
    return data


def pack_layout(template, data, *, max_size=MAX_LAYOUT):
    """Keep the verified retail DCX format; replace sizes and compressed data."""
    unpack_layout(template, max_size=max_size)
    if not 0 < len(data) <= max_size:
        raise ValueError('Invalid generated world-layout size')
    payload = zlib.compress(data, 9)
    header = bytearray(template[:0x4c])
    struct.pack_into('>II', header, 0x1c, len(data), len(payload))
    return bytes(header)+payload


def _lists(data):
    at = 0x10
    lists = []
    for number in range(4):
        if not 0 <= at <= len(data)-16:
            raise ValueError('Invalid MSB list header')
        count = struct.unpack_from('<i', data, at+4)[0]
        if not 1 <= count <= (len(data)-at-16)//8:
            raise ValueError('Invalid MSB list size')
        offsets = struct.unpack_from('<'+'q'*count, data, at+16)
        if any(not 0 < offset < len(data) for offset in offsets[:-1]):
            raise ValueError('Invalid MSB entry')
        lists.append((at+16, offsets[:-1]))
        at = offsets[-1]
        if number < 3 and not 0 < at < len(data):
            raise ValueError('Invalid MSB next list')
    return lists


def mirror_layout(msb):
    """BBHOST v0.2.24 dream_mirror_layout: exact known input and output only."""
    if hashlib.sha256(msb).hexdigest() != MIRROR_INPUT:
        raise ValueError('Not the retail 1.09 Hunter\'s Dream layout')
    data = bytearray(msb)
    lists = _lists(data)
    table, parts = lists[3]
    if len(parts) <= 808:
        raise ValueError('Too few Dream parts')
    starts = sorted(entry for _, entries in lists for entry in entries)
    def read(at):
        return struct.unpack_from('<i', data, at)[0]
    def write(at, value):
        struct.pack_into('<i', data, at, value)
    def remap(slot):
        return 742 if slot == 807 else slot+1 if 742 <= slot < 807 else slot
    for entries, fields in ((lists[1][1], (0x48,0x50,0x5c)),
                            (parts, (0x128,0x13c,0x144,0x178,0x180,0x188))):
        for entry in entries:
            index = bisect_right(starts, entry)
            size = (starts[index] if index < len(starts) else len(data))-entry
            for field in fields:
                if field+4 <= size:
                    slot = read(entry+field)
                    if slot >= 0 and remap(slot) != slot:
                        write(entry+field, remap(slot))
    for index, entry in enumerate(parts):
        kind, ordinal = read(entry+0x14), read(entry+0x18)
        if kind == 2 and index >= 742:
            write(entry+0x18, ordinal+1)
        if kind == 10 and index > 807:
            write(entry+0x18, ordinal-1)
    moved = parts[807]
    if read(moved+0x14) != 10:
        raise ValueError('Dream mirror is not a DummyEnemy')
    write(moved+0x14, 2)
    write(moved+0x18, 6)
    start, old, end = parts[742], moved, parts[808]
    if not start < old < end:
        raise ValueError('Dream parts are not ordered')
    size = end-old
    data[start:end] = data[old:end]+data[start:old]
    for index in range(742,807):
        struct.pack_into('<q', data, table+8*(index+1), parts[index]+size)
    struct.pack_into('<q', data, table+8*742, start)
    if hashlib.sha256(data).hexdigest() != MIRROR_OUTPUT:
        raise ValueError('Dream mirror rewrite does not match the known result')
    return bytes(data)


def build_mirror_mod(game, out):
    """Make/cache the generated layer; place it BEFORE player mods in an overlay.

    Returns a regular mods.build_overlay layer root, before player overrides.
    """
    source = Path(game)/MIRROR_PATH
    packed = source.read_bytes()
    rewritten = mirror_layout(unpack_layout(packed))
    root = Path(out)/'world-assets'/('mirror-'+MIRROR_OUTPUT)
    target = root/MIRROR_PATH
    if target.is_file():
        try:
            if unpack_layout(target.read_bytes()) == rewritten:
                return root
        except ValueError:
            pass
    generated = pack_layout(packed, rewritten)
    if unpack_layout(generated) != rewritten:
        raise ValueError('Generated mirror DCX did not round trip')
    target.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix='.mirror-', dir=target.parent)
    try:
        with os.fdopen(fd,'wb') as file:
            file.write(generated)
        os.replace(temporary, target)
    finally:
        Path(temporary).unlink(missing_ok=True)
    return root


def feature_layers(game, out, *, change_appearance=False, rebirth=False, rebirth_tool=None):
    """Build optional game enhancements before the selected player mod layers."""
    if not isinstance(change_appearance, bool) or not isinstance(rebirth, bool):
        raise ValueError('Enhancement selections must be boolean')
    layers = []
    if change_appearance:
        layers.append(('Cachyborne: Dream mirror', build_mirror_mod(game, out)))
    if rebirth:
        from rebirth_assets import build_rebirth_mod
        if rebirth_tool is None or not Path(rebirth_tool).is_file():
            raise ValueError('Build the altar helper first: bash build.sh')
        layers.append(('Cachyborne: Altar rebirth', build_rebirth_mod(game, Path(out)/'world-assets', rebirth_tool)))
    return layers
