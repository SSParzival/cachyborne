# SPDX-License-Identifier: GPL-2.0-or-later
"""Check ported menu layouts against the user's retail image when available."""
from paths import ROOT
import os
from pathlib import Path
import unittest

from patches import elf_read
from prepare import parse_self


class Retail024Offsets(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path = Path(os.environ.get('CACHYBORNE_TEST_EBOOT', ROOT / 'CUSA03173/eboot.bin'))
        if not path.is_file():
            raise unittest.SkipTest('retail 1.09 eboot unavailable')
        cls.elf, *_ = parse_self(path.read_bytes())

    def test_storage_constructor_layout_and_vtable(self):
        # lea rax,[rip+...]=image+0x539bd80; mov [r13],rax
        self.assertEqual(elf_read(self.elf, 0x1b9e13d, 11),
                         bytes.fromhex('48 8d 05 3c dc 7f 03 49 89 45 00'))
        # Selected inventory at +0x1490 and the RepositoryType wrapper at +0x1498.
        self.assertEqual(elf_read(self.elf, 0x1b9e1bc, 14),
                         bytes.fromhex('49 89 85 90 14 00 00 49 8d 9d 98 14 00 00'))
        self.assertEqual(elf_read(self.elf, 0x1b9e153, 7),
                         bytes.fromhex('49 8d 85 90 13 00 00'))

    def test_timing_function_bodies_match_upstream(self):
        for start, end, expected in ((0x1529470,0x15296c2,0x7e546384af3e3fc8),
                                     (0x152c390,0x152c930,0x65b4ccfbbc05261a)):
            body = elf_read(self.elf,start,end-start)
            h = 0xcbf29ce484222325
            for byte in body:
                h = ((h ^ byte) * 0x100000001b3) & ((1 << 64)-1)
            self.assertEqual(h,expected)
        self.assertEqual(elf_read(self.elf,0xbb2800,18),
                         bytes.fromhex('48 8b 05 c9 36 8b 04 c5 fa 11 80 ac 06 00 00 31 c0 c3'))
        self.assertEqual(elf_read(self.elf,0x6e464d,5),bytes.fromhex('e9 3e 55 00 00'))
        self.assertEqual(elf_read(self.elf,0xe5d6e1,8),bytes.fromhex('c5 fa 10 05 6f 1c a4 03'))

    def test_existing_list_entry(self):
        self.assertEqual(elf_read(self.elf, 0x1acff30, 6), bytes.fromhex('55 48 89 e5 41 57'))

    def test_sound_producer_sleep_and_before_limiter_observer(self):
        self.assertEqual(elf_read(self.elf,0x2801373,10),
                         bytes.fromhex('8b bb d4 08 00 00 29 c7 76 05'))
        self.assertEqual(elf_read(self.elf,0x280137d,5),bytes.fromhex('e8 3e a7 88 ff'))
        self.assertEqual(elf_read(self.elf,0x2034776,11),
                         bytes.fromhex('41 56 41 55 41 54 53 48 83 ec 38'))
        self.assertEqual(elf_read(self.elf,0x4733c51,5),b'33333')
        self.assertEqual(elf_read(self.elf,0x21629f1,4),(33000).to_bytes(4,'little'))

    def test_wind_loads_and_original_30fps_force(self):
        self.assertEqual(elf_read(self.elf,0x2313d8d,8),
                         bytes.fromhex('c5 fa 10 25 db 60 61 02'))
        self.assertEqual(elf_read(self.elf,0x23149ea,8),
                         bytes.fromhex('c5 79 6f b5 90 fe ff ff'))
        self.assertEqual(elf_read(self.elf,0x4929e70,4),bytes.fromhex('89 88 88 3c'))

    def test_gx_upstream_address_conversion(self):
        # Upstream sub_25b9980 uses BN's 0x400000 base. This verifies the
        # integration address for the next port; it does not prove it installed.
        self.assertEqual(elf_read(self.elf, 0x25b9980 - 0x400000, 17),
                         bytes.fromhex('55 48 89 e5 41 57 41 56 41 55 41 54 53 48 83 ec 18'))


if __name__ == '__main__':
    unittest.main()
