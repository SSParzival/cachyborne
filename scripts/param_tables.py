#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Inspect game tables and generate loose-file mods by PARAMDEF field names.

Adapted from droogie/bbhost tools/bbparam.py and engine/paramdef.cpp,
revision bdc5cdf8be5f37188da9c2368e92be444fdc04cb (GPL-3.0-or-later).
Adds bounded archive/row validation and reuses Cachyborne's DCX decoder.
The runtime table override service remains a separate integration.
"""
import argparse
from dataclasses import dataclass
import json
import math
import os
from pathlib import Path
import re
import struct
import tempfile
import tomllib

from world_assets import pack_layout, unpack_layout
from bnd4 import read as read_bnd4

FORMATS = {'s8': 'b', 'u8': 'B', 's16': 'h', 'u16': 'H', 's32': 'i', 'u32': 'I', 'f32': 'f'}
SIZES = {name: struct.calcsize(fmt) for name, fmt in FORMATS.items()} | {'dummy8': 1, 'fixstr': 1, 'fixstrW': 2}


def span(data, offset, size):
    if offset < 0 or size < 0 or offset > len(data) - size:
        raise ValueError('Truncated table or invalid offset')
    return data[offset:offset + size]


def number(data, fmt, offset):
    return struct.unpack('<' + fmt, span(data, offset, struct.calcsize('<' + fmt)))[0]


def text(data, offset, size):
    return span(data, offset, size).split(b'\0', 1)[0].decode('ascii')


def _named_entries(data):
    bundle = read_bnd4(data)
    entries = {}
    for entry in bundle.entries:
        name = entry.filename(bundle.unicode)
        if not name or name in entries:
            raise ValueError('Empty or duplicate archive entry')
        entries[name] = entry
    return entries


def entry_ranges(data):
    return {name: (entry.offset, len(entry.data)) for name, entry in _named_entries(data).items()}


def archive(path):
    packed = Path(path).read_bytes()
    data = unpack_layout(packed, max_size=64 << 20) if packed[:4] == b'DCX\0' else packed
    return {name: entry.data for name, entry in _named_entries(data).items()}


@dataclass(frozen=True)
class Field:
    name: str
    type: str
    offset: int
    size: int
    count: int = 1
    bit: int = 0
    bits: int = 0


@dataclass(frozen=True)
class Definition:
    type: str
    fields: tuple
    row_bytes: int


def definition(data):
    if number(data, 'H', 0x2e) != 201 or number(data, 'H', 0x0a) != 0xd0:
        raise ValueError('Unsupported PARAMDEF layout')
    count, start = number(data, 'H', 8), number(data, 'Q', 0x30)
    span(data, start, count * 0xd0)
    fields, offset, unit = [], 0, None
    for index in range(count):
        at = start + index * 0xd0
        primitive = text(data, at + 0x40, 8)
        declared = number(data, 'I', at + 0x64)
        match = re.fullmatch(r'([^\[\]:]+?)(?:\[(\d+)\])?(?::\s*(\d+))?', text(data, at + 0x90, 0x20).strip())
        if primitive not in SIZES or not match:
            raise ValueError('Unsupported PARAMDEF field')
        name, count, bits = match[1].strip(), int(match[2] or 1), int(match[3] or 0)
        size = SIZES[primitive]
        if count < 1 or bits > size * 8 or (bits and (count != 1 or primitive not in {*FORMATS, 'dummy8'} or primitive == 'f32')):
            raise ValueError('Invalid array or bitfield width')
        if bits:
            if unit is None or unit[1] != size or unit[2] + bits > size * 8:
                if unit is not None: offset += unit[1]
                unit = [offset, size, 0]
            fields.append(Field(name, primitive, offset, size, bit=unit[2], bits=bits))
            unit[2] += bits
        else:
            if unit is not None: offset += unit[1]; unit = None
            storage = declared or size * count
            if storage < size * count:
                raise ValueError('Field storage is smaller than its array')
            fields.append(Field(name, primitive, offset, storage, count))
            offset += storage
    if unit is not None: offset += unit[1]
    return Definition(text(data, 0x0c, 0x20), tuple(fields), offset)


def field_value(row, field):
    data = span(row, field.offset, field.size)
    if field.bits:
        return (int.from_bytes(data, 'little') >> field.bit) & ((1 << field.bits) - 1)
    if field.type == 'fixstr': return data.split(b'\0', 1)[0].decode('ascii', 'replace')
    if field.type == 'fixstrW': return data.decode('utf-16le').split('\0', 1)[0]
    if field.type == 'dummy8': return None
    size = SIZES[field.type]
    values = [number(data, FORMATS[field.type], index * size) for index in range(field.count)]
    return values[0] if field.count == 1 else values


def table_rows(data, layout):
    if text(data, 0x0c, 0x20) != layout.type:
        raise ValueError('Table and PARAMDEF types differ')
    count = number(data, 'H', 0x0a)
    wide = bool(number(data, 'B', 0x2e) & 2)
    stride = 0x18 if wide else 0x0c
    span(data, 0x40, count * stride)
    records = [(number(data, 'I', 0x40 + i * stride),
                number(data, 'Q' if wide else 'I', 0x40 + i * stride + (8 if wide else 4))) for i in range(count)]
    offsets = sorted(set(offset for _, offset in records))
    if len(offsets) > 1 and any(b - a != layout.row_bytes for a, b in zip(offsets, offsets[1:])):
        raise ValueError('Row stride differs from the field definition')
    # Several retail tables contain repeated IDs. Preserve records in order;
    # converting them into a dictionary would silently discard rows.
    result = []
    for key, offset in records:
        if offset < 0x40 + count * stride:
            raise ValueError('Row overlaps the record table')
        row = span(data, offset, layout.row_bytes)
        result.append((key, {field.name: field_value(row, field) for field in layout.fields
                             if field.type != 'dummy8' and not field.name.startswith('pad')}))
    return result


def load_table_data(game, name):
    """Return table bytes and their matching field definition.

    Consumers validate record boundaries through table_rows/apply_overrides.
    """
    root = Path(game) / 'dvdroot_ps4'
    params = archive(root / 'param/gameparam/gameparam.parambnd.dcx')
    raw = params[name + '.param']
    wanted = text(raw, 0x0c, 0x20)
    for data in archive(root / 'paramdef/paramdef.paramdefbnd.dcx').values():
        if text(data, 0x0c, 0x20) == wanted:
            layout = definition(data)
            return raw, layout
    raise ValueError(f'No field definition for {wanted}')


def load_table(game, name):
    raw, layout = load_table_data(game, name)
    return table_rows(raw, layout)


def write_field(row, field, value, index=0):
    """Write one numeric field, retaining every neighboring bit and array element."""
    span(row, field.offset, field.size)
    if not 0 <= index < field.count or field.type not in FORMATS:
        raise ValueError('Field is not writable or array index is out of range')
    if field.type == 'f32':
        try:
            finite = isinstance(value, (int, float)) and math.isfinite(value)
        except OverflowError:
            finite = False
        if not finite:
            raise ValueError('Float field requires a finite number')
    elif not isinstance(value, int):
        raise ValueError('Integer field requires an integer')
    if field.bits:
        if not 0 <= value < (1 << field.bits):
            raise ValueError('Bitfield value exceeds its width')
        unit = int.from_bytes(row[field.offset:field.offset + field.size], 'little')
        mask = ((1 << field.bits) - 1) << field.bit
        row[field.offset:field.offset + field.size] = ((unit & ~mask) | (value << field.bit)).to_bytes(field.size, 'little')
    else:
        try:
            packed = struct.pack('<' + FORMATS[field.type], value)
        except (struct.error, OverflowError) as error:
            raise ValueError('Value does not fit field storage') from error
        at = field.offset + index * SIZES[field.type]
        row[at:at + len(packed)] = packed


def apply_overrides(data, layout, overrides):
    """BBHOST-style [row.all]/[row.ID] named field edits, transactionally in memory."""
    rows = table_rows(data, layout)
    changes = overrides.get('row')
    if set(overrides) != {'row'} or not isinstance(changes, dict) or not changes:
        raise ValueError('Expected nonempty [row.ID] or [row.all] sections')
    ids = {str(key) for key, _ in rows}
    if any(key != 'all' and key not in ids for key in changes):
        raise ValueError('Override names a row absent from the table')
    fields = {field.name: field for field in layout.fields}
    edits = {}
    for key, values in changes.items():
        if not isinstance(values, dict) or not values:
            raise ValueError('Row override must contain named fields')
        edits[key] = []
        for name, value in values.items():
            match = re.fullmatch(r'([^\[\]]+)(?:\[(\d+)\])?', name)
            if not match or match[1] not in fields:
                raise ValueError(f'Unknown field: {name}')
            field, index = fields[match[1]], int(match[2] or 0)
            if field.count != 1 and match[2] is None:
                raise ValueError('Array edits must name one element')
            # Validate every edit even if a later per-ID edit supersedes it.
            write_field(bytearray(layout.row_bytes), field, value, index)
            edits[key].append((field, value, index))
    out = bytearray(data)
    wide = bool(number(data, 'B', 0x2e) & 2)
    stride = 0x18 if wide else 0x0c
    for ordinal, (key, _) in enumerate(rows):
        at = number(data, 'Q' if wide else 'I', 0x40 + ordinal * stride + (8 if wide else 4))
        row = bytearray(span(out, at, layout.row_bytes))
        for field, value, index in [*edits.get('all', ()), *edits.get(str(key), ())]:
            write_field(row, field, value, index)
        out[at:at + layout.row_bytes] = row
    return bytes(out)


def build_mod(game, table, overrides, output):
    """Generate one loose-file mod; original dump and existing outputs stay intact."""
    return build_mods(game, {table: overrides}, output)


def build_mods(game, tables, output):
    """Combine table edits into one archive so separate table mods do not conflict."""
    if not tables:
        raise ValueError('No table overrides supplied')
    game, output = Path(game).resolve(), Path(output).resolve()
    relative = Path('dvdroot_ps4/param/gameparam/gameparam.parambnd.dcx')
    target = output / relative
    if target.resolve().is_relative_to(game) or target.exists():
        raise ValueError('Output must be a new mod outside the game dump')
    packed = (game / relative).read_bytes()
    data = bytearray(unpack_layout(packed, max_size=64 << 20))
    entries = entry_ranges(data)
    definitions = archive(game / 'dvdroot_ps4/paramdef/paramdef.paramdefbnd.dcx')
    layouts = {}
    for table, overrides in tables.items():
        at, size = entries[table + '.param']
        raw = bytes(span(data, at, size))
        wanted = text(raw, 0x0c, 0x20)
        if wanted not in layouts:
            layouts[wanted] = next((definition(value) for value in definitions.values() if text(value, 0x0c, 0x20) == wanted), None)
        if layouts[wanted] is None: raise ValueError('Table has no field definition')
        data[at:at + size] = apply_overrides(raw, layouts[wanted], overrides)
    generated = pack_layout(packed, data, max_size=64 << 20)
    if unpack_layout(generated, max_size=64 << 20) != data:
        raise ValueError('Generated archive did not round trip')
    target.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix='.param-', dir=target.parent)
    try:
        with os.fdopen(fd, 'wb') as file: file.write(generated)
        os.link(temporary, target)  # Publish only if no existing mod occupies the path.
    finally:
        Path(temporary).unlink(missing_ok=True)
    return target


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('table', nargs='?')
    parser.add_argument('--app0', type=Path, required=True)
    parser.add_argument('--id', type=int)
    parser.add_argument('--fields', help='Comma separated field names')
    edits = parser.add_mutually_exclusive_group()
    edits.add_argument('--overrides', type=Path, help='BBHOST-style TOML named row edits')
    edits.add_argument('--overrides-dir', type=Path, help='Combine TABLE.toml files into one mod')
    parser.add_argument('--out', type=Path, help='New loose-file mod directory for --overrides')
    args = parser.parse_args()
    generating = args.overrides is not None or args.overrides_dir is not None
    if generating != (args.out is not None):
        parser.error('--overrides/--overrides-dir requires --out')
    if generating and (args.id is not None or args.fields is not None):
        parser.error('--id/--fields select inspection; --overrides selects mod generation')
    if args.overrides_dir is not None and args.table is not None:
        parser.error('--overrides-dir uses the TABLE.toml names; omit the table argument')
    if args.overrides_dir is None and args.table is None:
        parser.error('table is required for inspection or --overrides')
    try:
        if args.overrides_dir is not None:
            tables = {}
            for path in sorted(args.overrides_dir.glob('*.toml')):
                with path.open('rb') as file: tables[path.stem] = tomllib.load(file)
            print(build_mods(args.app0, tables, args.out))
            return
        if args.overrides is not None:
            with args.overrides.open('rb') as file: overrides = tomllib.load(file)
            print(build_mod(args.app0, args.table, overrides, args.out))
            return
        rows = load_table(args.app0, args.table)
        wanted = args.fields.split(',') if args.fields else None
        if args.id is not None and not any(key == args.id for key, _ in rows):
            raise ValueError(f'Row {args.id} is absent from the table')
        for key, row in rows:
            if args.id is None or key == args.id:
                if wanted and any(name not in row for name in wanted):
                    raise ValueError('Requested field is absent from the table')
                print(json.dumps(dict(id=key, fields={name: value for name, value in row.items()
                                                      if wanted is None or name in wanted}), ensure_ascii=False))
    except (OSError, ValueError, KeyError, struct.error) as error:
        parser.exit(1, f'Cannot read table: {error}\n')


if __name__ == '__main__':
    main()
