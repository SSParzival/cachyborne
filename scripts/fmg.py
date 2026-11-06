# SPDX-License-Identifier: GPL-3.0-or-later
"""Bounded version-2 UTF-16 message tables, adapted from BBHOST menu_assets.cpp
at bdc5cdf8be5f37188da9c2368e92be444fdc04cb. Preserve null versus empty strings.
"""
import struct
from bnd4 import MAX_ARCHIVE, number, span, terminated


def read(data):
    if len(data) > MAX_ARCHIVE or span(data, 0, 4) != b'\0\0\2\0' or number(data, 'I', 4) != len(data):
        raise ValueError('Unsupported or truncated FMG version 2')
    groups, count, table = number(data, 'I', 0x0c), number(data, 'I', 0x10), number(data, 'Q', 0x18)
    span(data, 0x28, groups * 16)
    span(data, table, count * 8)
    body = table + count * 8
    if table < 0x28 + groups * 16:
        raise ValueError('Overlapping FMG tables')
    messages, strings, previous, used, budget = {}, {}, -0x80000001, set(), len(data) * 4
    for g in range(groups):
        index, first, last = struct.unpack('<iii', span(data, 0x28 + g * 16, 12))
        n = last - first + 1
        if first <= previous or n < 1 or index < 0 or n > count - index:
            raise ValueError('Invalid or overlapping FMG group')
        previous = last
        for k in range(n):
            if index + k in used:
                raise ValueError('Overlapping FMG string indices')
            used.add(index + k)
            offset = number(data, 'Q', table + (index + k) * 8)
            if not offset:
                value = None
            else:
                if offset < body or offset % 2:
                    raise ValueError('FMG string overlaps metadata or is unaligned')
                if offset not in strings:
                    # Bound work even for distinct offsets into a shared long string.
                    end = min(len(data), offset + budget + 2)
                    text = terminated(data, offset, end, 2)
                    budget -= len(text) + 2
                    if budget < 0:
                        raise ValueError('FMG string work limit exceeded')
                    strings[offset] = text.decode('utf-16le', errors='surrogatepass')
                value = strings[offset]
            messages[first + k] = value
    return messages


def write(messages):
    if any(type(k) is not int or not -0x80000000 <= k <= 0x7fffffff for k in messages):
        raise ValueError('Invalid FMG message ID')
    keys = sorted(messages)
    groups = []
    for i, key in enumerate(keys):
        if not groups or key != groups[-1][2] + 1:
            groups.append([i, key, key])
        else:
            groups[-1][2] = key
    table = 0x28 + len(groups) * 16
    body = table + len(keys) * 8
    if body > MAX_ARCHIVE:
        raise ValueError('FMG metadata exceeds size limit')
    out = bytearray(body)
    out[2] = 2
    struct.pack_into('<IIII', out, 8, 1, len(groups), len(keys), 0xff)
    struct.pack_into('<Q', out, 0x18, table)
    for i, group in enumerate(groups):
        struct.pack_into('<iii', out, 0x28 + i * 16, *group)
    offsets = {}
    for i, key in enumerate(keys):
        text = messages[key]
        if text is None:
            continue
        if not isinstance(text, str) or '\0' in text or len(text) > MAX_ARCHIVE // 2:
            raise ValueError('Invalid FMG string')
        if text not in offsets:
            offsets[text] = len(out)
            encoded = text.encode('utf-16le', errors='surrogatepass') + b'\0\0'
            if len(out) + len(encoded) > MAX_ARCHIVE:
                raise ValueError('Generated FMG exceeds size limit')
            out += encoded
        struct.pack_into('<Q', out, table + i * 8, offsets[text])
    struct.pack_into('<I', out, 4, len(out))
    return bytes(out)
