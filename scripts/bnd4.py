# SPDX-License-Identifier: GPL-3.0-or-later
"""Bounded, uncompressed retail BND4 read/write for generated asset layers.

Adapted from BBHOST gcn/container.cpp at bdc5cdf8be5f37188da9c2368e92be444fdc04cb.
Preserves names, entry flags/IDs and the original header. Payloads align to
absolute 16-byte offsets, as in retail talk archives.
"""
from dataclasses import dataclass
import struct

MAX_ARCHIVE = 64 << 20


def span(data, offset, size):
    if offset < 0 or size < 0 or offset > len(data) - size:
        raise ValueError('Asset offset or size outside the file')
    return data[offset:offset + size]


def number(data, fmt, offset):
    return struct.unpack('<' + fmt, span(data, offset, struct.calcsize('<' + fmt)))[0]


def terminated(data, offset, end, width):
    if offset < 0 or end > len(data) or offset >= end:
        raise ValueError('Asset string outside the file')
    stop = offset
    while stop <= end - width:
        if not any(data[stop:stop + width]):
            return data[offset:stop]
        stop += width
    raise ValueError('Unterminated asset string')


@dataclass(frozen=True)
class Entry:
    flags: int
    id: int
    name: bytes
    data: bytes
    offset: int = 0

    def filename(self, unicode=True):
        return self.name.decode('utf-16le' if unicode else 'ascii').replace('\\', '/').rsplit('/', 1)[-1]


@dataclass(frozen=True)
class Archive:
    header: bytes
    entries: tuple

    @property
    def unicode(self):
        return bool(self.header[0x30])


def read(data):
    if len(data) > MAX_ARCHIVE or span(data, 0, 4) != b'BND4':
        raise ValueError('Expected a bounded BND4 archive')
    data = bytes(data)
    header = span(data, 0, 0x40)
    count, table, stride, start = (number(data, fmt, at) for fmt, at in
                                 [('I', 0x0c), ('Q', 0x10), ('Q', 0x20), ('Q', 0x28)])
    if table != 0x40 or stride != 0x24 or header[6:8] != b'\0\0' or any(header[0x38:0x40]):
        raise ValueError('Unsupported BND4 layout, endianness or hash table')
    span(data, table, count * stride)
    names_start = table + count * stride
    if not names_start <= start <= len(data):
        raise ValueError('Invalid BND4 names/data boundary')
    entries, names = [], set()
    for i in range(count):
        at = table + i * stride
        flags, packed, size, offset, key, name_at = struct.unpack('<QQQIII', span(data, at, stride))
        if packed != size or flags & 0xff != 0x40:
            raise ValueError('Compressed/unsupported BND4 entry')
        if name_at < names_start or offset < start or offset % 16:
            raise ValueError('BND4 entry overlaps metadata or is unaligned')
        name = terminated(data, name_at, min(start, name_at + 4096), 2 if header[0x30] else 1)
        if not name or name in names:
            raise ValueError('Empty or duplicate BND4 name')
        names.add(name)
        entry = Entry(flags, key, name, span(data, offset, size), offset)
        entry.filename(bool(header[0x30]))  # reject invalid text before publication
        entries.append(entry)
    return Archive(header, tuple(entries))


def write(archive):
    if len(archive.header) != 0x40 or archive.header[:4] != b'BND4':
        raise ValueError('Invalid BND4 header')
    stride = 0x24
    names_start = 0x40 + len(archive.entries) * stride
    if names_start > MAX_ARCHIVE:
        raise ValueError('BND4 metadata exceeds size limit')
    names, offsets, unique = bytearray(), [], set()
    for entry in archive.entries:
        if not entry.name or entry.name in unique or len(entry.name) > 4094:
            raise ValueError('Invalid or duplicate BND4 name')
        width = 2 if archive.unicode else 1
        entry.filename(archive.unicode)
        if len(entry.name) % width or any(not any(entry.name[i:i+width]) for i in range(0, len(entry.name), width)):
            raise ValueError('Embedded BND4 name terminator')
        unique.add(entry.name)
        offsets.append(names_start + len(names))
        names += entry.name + bytes(width)
        if names_start + len(names) > MAX_ARCHIVE:
            raise ValueError('BND4 names exceed size limit')
    start = names_start + len(names)
    out = bytearray(archive.header)
    struct.pack_into('<I', out, 0x0c, len(archive.entries))
    struct.pack_into('<Q', out, 0x28, start)
    out += bytes(len(archive.entries) * stride) + names
    for i, entry in enumerate(archive.entries):
        out += bytes((-len(out)) % 16)
        if len(out) + len(entry.data) > MAX_ARCHIVE:
            raise ValueError('Generated BND4 exceeds size limit')
        struct.pack_into('<QQQIII', out, 0x40 + i * stride, entry.flags,
                         len(entry.data), len(entry.data), len(out), entry.id, offsets[i])
        out += entry.data
    # Apply the same validation to caller-constructed archives.
    read(out)
    return bytes(out)
