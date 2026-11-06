"""PARAMDEF layout, bit packing, row boundaries and actual retail lamp fields."""
from paths import ROOT
import struct
import hashlib
from pathlib import Path
import tempfile
import unittest

import param_tables as params


class ParamTableTests(unittest.TestCase):
    def test_bad_archive_counts_and_unterminated_names_are_bounded(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'bad.bnd'
            for name_offset, count in ((0x60, 1), (0x1000, 1), (0x40, 0xffffffff)):
                data = bytearray(b'X' * 0x80)
                data[:4] = b'BND4'
                struct.pack_into('<I', data, 0x0c, count)
                struct.pack_into('<Q', data, 0x20, 0x24)
                struct.pack_into('<I', data, 0x60, name_offset)
                path.write_bytes(data)
                with self.assertRaises(ValueError): params.archive(path)

    def layout(self):
        data = bytearray(0x40 + 5 * 0xd0)
        struct.pack_into('<HH', data, 8, 5, 0xd0)
        data[0x0c:0x10] = b'TEST'
        struct.pack_into('<HQ', data, 0x2e, 201, 0x40)
        for i, (primitive, name, size) in enumerate([
                ('u8', 'flag:1', 1), ('u8', 'mode:3', 1),
                ('s16', 'vector[2]', 4), ('f32', 'scale', 4), ('fixstrW', 'label', 8)]):
            at = 0x40 + i * 0xd0
            data[at + 0x40:at + 0x40 + len(primitive)] = primitive.encode()
            data[at + 0x90:at + 0x90 + len(name)] = name.encode()
            struct.pack_into('<I', data, at + 0x64, size)
        return data

    def test_packing_arrays_and_strings(self):
        layout = params.definition(self.layout())
        self.assertEqual(layout.row_bytes, 17)
        row = bytes([0b1011]) + struct.pack('<hhf', -7, 300, 1.25) + 'Luz\0'.encode('utf-16le')
        fields = {field.name: params.field_value(row, field) for field in layout.fields}
        self.assertEqual(fields, dict(flag=1, mode=5, vector=[-7, 300], scale=1.25, label='Luz'))

    def test_edits_preserve_neighbor_bits_and_array_elements(self):
        layout = params.definition(self.layout())
        row = bytearray(bytes([0b11111011]) + struct.pack('<hhf', -7, 300, 1.25) + 'Luz\0'.encode('utf-16le'))
        original = bytes(row)
        params.write_field(row, layout.fields[1], 2)
        self.assertEqual(row[0], 0b11110101)
        self.assertEqual(row[1:], original[1:])
        params.write_field(row, layout.fields[2], -200, 1)
        self.assertEqual(params.field_value(row, layout.fields[2]), [-7, -200])
        for field, value, index in ((layout.fields[1], 8, 0), (layout.fields[2], 99999, 0),
                                     (layout.fields[2], 1, 2), (layout.fields[3], float('nan'), 0),
                                     (layout.fields[4], 'x', 0)):
            before = bytes(row)
            with self.assertRaises(ValueError): params.write_field(row, field, value, index)
            self.assertEqual(row, before)

    def test_definitions_refuse_unknown_types_truncation_and_impossible_bits(self):
        for bad in (self.layout()[:-1], bytearray(self.layout())):
            if len(bad) == len(self.layout()):
                bad[0x40 + 0x90:0x40 + 0x96] = b'flag:9'
            with self.assertRaises(ValueError): params.definition(bad)
        bad = self.layout();struct.pack_into('<H', bad, 0x2e, 200)
        with self.assertRaises(ValueError): params.definition(bad)

    def test_rows_cannot_read_headers_neighbors_or_wrong_definitions(self):
        layout = params.definition(self.layout())
        data = bytearray(0x80 + 2 * layout.row_bytes)
        data[0x0c:0x10] = b'TEST'
        struct.pack_into('<H', data, 0x0a, 2)
        struct.pack_into('<II', data, 0x40, 1, 0x80)
        struct.pack_into('<II', data, 0x4c, 2, 0x80 + layout.row_bytes)
        self.assertEqual(len(params.table_rows(data, layout)), 2)
        for offset in (0x40, 0x81 + layout.row_bytes, len(data)):
            bad = bytearray(data);struct.pack_into('<I', bad, 0x50, offset)
            with self.assertRaises(ValueError): params.table_rows(bad, layout)
        with self.assertRaises(ValueError): params.table_rows(data[:-1], layout)

    def test_retail_lamps_by_field_name(self):
        game = ROOT / 'CUSA03173'
        if not (game / 'dvdroot_ps4/param/gameparam/gameparam.parambnd.dcx').is_file():
            self.skipTest('Retail dump not available')
        rows = dict(params.load_table(game, 'ReturnPointParam'))
        self.assertEqual((rows[3202950]['areaNo'], rows[3202950]['blockNo']), (32, 0))
        self.assertEqual((rows[2402950]['areaNo'], rows[2402950]['blockNo']), (24, 0))
        self.assertEqual(rows[3202950]['returnPointEntityId'], 3202950)

    def test_retail_definitions_and_every_table_decode(self):
        root = ROOT / 'CUSA03173/dvdroot_ps4'
        if not (root / 'param/gameparam/gameparam.parambnd.dcx').is_file():
            self.skipTest('Retail dump not available')
        definitions = {layout.type: layout for layout in
                       (params.definition(data) for data in params.archive(root / 'paramdef/paramdef.paramdefbnd.dcx').values())}
        tables = params.archive(root / 'param/gameparam/gameparam.parambnd.dcx')
        self.assertEqual(len(definitions), 63)
        self.assertEqual(len(tables), 65)
        total = 0
        for name, data in tables.items():
            with self.subTest(table=name):
                rows = params.table_rows(data, definitions[params.text(data, 0x0c, 0x20)])
                self.assertEqual(len(rows), params.number(data, 'H', 0x0a))
                total += len(rows)
        self.assertGreater(total, 60000)

    def test_retail_mod_round_trip_and_source_integrity(self):
        game = ROOT / 'CUSA03173'
        source = game / 'dvdroot_ps4/param/gameparam/gameparam.parambnd.dcx'
        if not source.is_file(): self.skipTest('Retail dump not available')
        before = hashlib.sha256(source.read_bytes()).digest()
        original = params.archive(source)
        defs = params.archive(game / 'dvdroot_ps4/paramdef/paramdef.paramdefbnd.dcx')
        raw = original['ReturnPointParam.param']
        layout = next(params.definition(d) for d in defs.values() if params.text(d, 0x0c, 0x20) == params.text(raw, 0x0c, 0x20))
        changes = {'row': {'all': {'returnAnimId': 7}, '3202950': {'returnAnimId': 8}}}
        with tempfile.TemporaryDirectory() as folder:
            target = params.build_mod(game, 'ReturnPointParam', changes, Path(folder) / 'mod')
            generated = params.archive(target)
            self.assertEqual(set(generated), set(original))
            for name in original:
                if name != 'ReturnPointParam.param': self.assertEqual(generated[name], original[name])
            rows = params.table_rows(generated['ReturnPointParam.param'], layout)
            self.assertTrue(all(row['returnAnimId'] == (8 if key == 3202950 else 7) for key, row in rows))
            with self.assertRaises(ValueError): params.build_mod(game, 'ReturnPointParam', changes, Path(folder) / 'mod')
            for bad in ({'row': {'3202950': {'missing': 1}}}, {'row': {'99999999': {'returnAnimId': 7}}}):
                with self.assertRaises(ValueError): params.apply_overrides(raw, layout, bad)
            multi = params.build_mods(game, {'ReturnPointParam': changes,
                'LockCamParam': {'row': {'all': {'camDistTarget': 6.5}}}}, Path(folder) / 'multi')
            combined = params.archive(multi)
            cam_layout = next(params.definition(d) for d in defs.values() if
                              params.text(d, 0x0c, 0x20) == params.text(combined['LockCamParam.param'], 0x0c, 0x20))
            self.assertTrue(all(row['camDistTarget'] == 6.5 for _, row in params.table_rows(combined['LockCamParam.param'], cam_layout)))
            self.assertEqual(combined['ReturnPointParam.param'], generated['ReturnPointParam.param'])
            for name in original:
                if name not in ('ReturnPointParam.param', 'LockCamParam.param'):
                    self.assertEqual(combined[name], original[name])
            rejected = Path(folder) / 'rejected'
            with self.assertRaises(ValueError):
                params.build_mods(game, {'ReturnPointParam': changes,
                    'LockCamParam': {'row': {'all': {'unknown': 1}}}}, rejected)
            self.assertFalse(rejected.exists())
            with self.assertRaises(ValueError): params.build_mod(game, 'ReturnPointParam', changes, game)
        self.assertEqual(hashlib.sha256(source.read_bytes()).digest(), before)


if __name__ == '__main__':
    unittest.main()
