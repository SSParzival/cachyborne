# SPDX-License-Identifier: GPL-2.0-or-later
from paths import ROOT  # noqa: F401
from pathlib import Path
import os
import tempfile
import unittest
from isolated_game import SnapshotGate


class SnapshotBeforeReplay(unittest.TestCase):
    def test_missing_partial_and_complete_image_need_quiet_period(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);gate=SnapshotGate(root,'4x2');trigger=root/'trigger'
            self.assertFalse(gate.ready(0))
            gate.request(trigger,1);self.assertTrue(trigger.exists())
            self.assertFalse(gate.ready(2))
            image=root/'f000_final_4x2_bgra.raw';image.write_bytes(bytes(31))
            self.assertFalse(gate.ready(3))
            image.write_bytes(bytes(32));self.assertFalse(gate.ready(4))
            self.assertFalse(gate.ready(5.99));self.assertTrue(gate.ready(6))
            # A later write restarts the quiet interval, even at the same size.
            stat=image.stat();os.utime(image,ns=(stat.st_atime_ns,stat.st_mtime_ns+1))
            self.assertFalse(gate.ready(7));self.assertTrue(gate.ready(9))

    def test_stale_oversized_wrong_size_and_missing_images_cannot_start_replay(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);image=root/'f000_final_4x2_rgba.raw'
            image.write_bytes(bytes(32));gate=SnapshotGate(root,'4x2')
            with self.assertRaisesRegex(RuntimeError,'already exists'):gate.request(root/'trigger',1)
            image.unlink();gate.request(root/'trigger',1)
            image.write_bytes(bytes(33))
            with self.assertRaisesRegex(RuntimeError,'exceeds'):gate.ready(2)
            image.unlink()
            (root/'f000_final_3x2_bgra.raw').write_bytes(bytes(24))
            with self.assertRaisesRegex(RuntimeError,'timeout'):gate.ready(32)

    def test_rgba_and_bgra_are_supported_without_duplicate_request(self):
        for fmt in ('rgba','bgra'):
            with tempfile.TemporaryDirectory() as folder:
                root=Path(folder);gate=SnapshotGate(root,'4x2');gate.request(root/'trigger',1)
                with self.assertRaisesRegex(RuntimeError,'already requested'):gate.request(root/'trigger',2)
                (root/f'f000_final_4x2_{fmt}.raw').write_bytes(bytes(32))
                self.assertFalse(gate.ready(3));self.assertTrue(gate.ready(5))
