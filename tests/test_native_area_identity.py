# SPDX-License-Identifier: GPL-2.0-or-later
import sys
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from verify_native import requested_area, target_rate_gate


class NativeAreaIdentity(unittest.TestCase):
    @patch('verify_native.load_table', return_value=[
        (2602950, {'areaNo': 26, 'blockNo': 0}),
        (3202950, {'areaNo': 32, 'blockNo': 0}),
        (2412951, {'areaNo': 24, 'blockNo': 1})])
    def test_lamp_map_identity(self, load):
        self.assertEqual(requested_area('dump', 2602950, 0x1a000000)['name'], 'Nightmare of Mensis')
        self.assertEqual(requested_area('dump', 3202950, 0x20000000)['name'], 'Byrgenwerth')
        self.assertEqual(requested_area('dump', 2412951, 0x18010000)['block'], 'm24_01_00_00')
        with self.assertRaisesRegex(ValueError, 'differs'):
            requested_area('dump', 2602950, 0x20000000)
        with self.assertRaisesRegex(ValueError, 'absent'):
            requested_area('dump', 2702950, 0x1b000000)

    def test_no_lamp_does_not_read_dump(self):
        with patch('verify_native.load_table') as load:
            self.assertIsNone(requested_area('dump', None, None))
            load.assert_not_called()

    def test_target_rate_does_not_accept_old_relative_pacer(self):
        self.assertFalse(target_rate_gate({'measured_fps':59.825,'window_fps':59.8167},120))
        self.assertTrue(target_rate_gate({'measured_fps':59.992,'window_fps':60.0},120))
        self.assertFalse(target_rate_gate({'measured_fps':60.2,'window_fps':60.2},120))
        self.assertFalse(target_rate_gate({'measured_fps':59.99,'window_fps':59.8},120))
        self.assertFalse(target_rate_gate({},120))

    def test_short_window_has_frame_count_granularity(self):
        self.assertTrue(target_rate_gate({'measured_fps':59.99,'window_fps':59.5},2))
        self.assertFalse(target_rate_gate({'measured_fps':59.99,'window_fps':59},2))


if __name__ == '__main__':
    unittest.main()
