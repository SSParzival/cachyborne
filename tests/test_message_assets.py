# SPDX-License-Identifier: GPL-2.0-or-later
from paths import ROOT
from dataclasses import replace
import struct
import unittest

import bnd4
import fmg
from world_assets import unpack_layout


class MessageAssets(unittest.TestCase):
    def bundle(self):
        header = bytearray(0x40)
        header[:4] = b'BND4'
        struct.pack_into('<Q', header, 0x10, 0x40)
        struct.pack_into('<Q', header, 0x20, 0x24)
        header[0x30] = 1
        return bnd4.Archive(bytes(header), (
            bnd4.Entry(0xffffffff00000040, 30, 'イベント.fmg'.encode('utf-16le'), b'12345'),
            bnd4.Entry(0xffffffff00000040, 200, 'Menu.fmg'.encode('utf-16le'), b'abcdef'),
        ))

    def test_bnd_resize_preserves_metadata_and_aligns_absolute_offsets(self):
        original = self.bundle()
        encoded = bnd4.write(original)
        parsed = bnd4.read(encoded)
        self.assertEqual(bnd4.write(parsed), encoded)
        changed = replace(parsed, entries=(replace(parsed.entries[0], data=b'x' * 103), parsed.entries[1]))
        reread = bnd4.read(bnd4.write(changed))
        self.assertEqual(reread.entries[0].data, b'x' * 103)
        self.assertEqual(reread.entries[1].data, b'abcdef')
        self.assertEqual([(e.name, e.id, e.flags) for e in reread.entries],
                         [(e.name, e.id, e.flags) for e in parsed.entries])
        self.assertTrue(all(e.offset % 16 == 0 for e in reread.entries))

    def test_bnd_rejects_bad_counts_sizes_names_and_compression(self):
        good = bnd4.write(self.bundle())
        for offset, fmt, value in [(0xc,'I',0xffffffff), (0x28,'Q',0xffffffffffffffff),
                                  (0x48,'Q',0xffffffffffffffff), (0x58,'I',0xffffffff),
                                  (0x60,'I',0xffffffff), (0x40,'Q',2), (0x38,'Q',1)]:
            bad = bytearray(good)
            struct.pack_into('<'+fmt,bad,offset,value)
            with self.subTest(offset=offset), self.assertRaises(ValueError): bnd4.read(bad)
        for n in range(0x64):
            with self.assertRaises(ValueError): bnd4.read(good[:n])
        with self.assertRaises(ValueError):
            bnd4.write(replace(self.bundle(), entries=(self.bundle().entries[0],)*2))

    def test_fmg_unicode_null_empty_and_shared_strings(self):
        messages = {-1:None, 0:'', 1:'A가', 2:'Luz 🌙', 99:'A가', 0x7fffffff:'fin'}
        encoded = fmg.write(messages)
        self.assertEqual(fmg.read(encoded), messages)
        self.assertEqual(fmg.read(fmg.write({})), {})
        self.assertEqual(fmg.write(fmg.read(encoded)), encoded)

    def test_fmg_rejects_overlap_unterminated_and_overflowing_ranges(self):
        good = fmg.write({1:'abc', 5:'def'})
        for offset, fmt, value in [(0xc,'I',0xffffffff), (0x10,'I',0xffffffff),
                                  (0x18,'Q',0xffffffffffffffff), (0x28,'i',-1),
                                  (0x30,'i',0x7fffffff), (0x3c,'i',1)]:
            bad=bytearray(good);struct.pack_into('<'+fmt,bad,offset,value)
            with self.subTest(offset=offset), self.assertRaises(ValueError): fmg.read(bad)
        bad=bytearray(good);bad[-2:]=b'xx'
        with self.assertRaises(ValueError): fmg.read(bad)
        for messages in ({2**31:'x'}, {'1':'x'}, {1:'embedded\0null'}, {1:123}):
            with self.assertRaises(ValueError): fmg.write(messages)

    def test_local_talk_and_message_archives(self):
        game = ROOT/'CUSA03173/dvdroot_ps4'
        paths = sorted((game/'script/talk').glob('*.talkesdbnd.dcx'))
        paths += sorted((game/'msg').glob('*/menu.msgbnd.dcx'))
        if not paths: self.skipTest('No local game dump')
        for path in paths:
            raw=unpack_layout(path.read_bytes(), max_size=bnd4.MAX_ARCHIVE)
            bundle=bnd4.read(raw)
            self.assertEqual(bnd4.write(bundle),raw,path.name)
            if path.parent.parent.name=='msg':
                for entry in bundle.entries:
                    messages=fmg.read(entry.data)
                    self.assertEqual(fmg.read(fmg.write(messages)),messages,(path,entry.id))
