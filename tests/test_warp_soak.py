# SPDX-License-Identifier: GPL-2.0-or-later
import importlib.util
from paths import ROOT
from pathlib import Path
import tempfile
import unittest
from isolated_game import fault_checks

SPEC = importlib.util.spec_from_file_location('warp_soak', ROOT / 'scripts/stress_warp_soak.py')
soak = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(soak)

class WarpEvidenceTests(unittest.TestCase):
    def test_power_comes_from_one_known_device_and_missing_values_stay_unknown(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory); device=root/'0000:03:00.0'
            sensor=device/'hwmon/hwmon7/power1_average'
            sensor.parent.mkdir(parents=True);sensor.write_text('28000000\n')
            (device/'gpu_busy_percent').write_text('13\n')
            self.assertEqual(soak.gpu_sample({'0000:03:00.0'},root),dict(gpu_power_uw=28000000,gpu_busy_percent=13))
            for devices in (set(), {'0000:03:00.0','0000:04:00.0'}, {'../escape'}):
                self.assertEqual(soak.gpu_sample(devices,root),dict(gpu_power_uw=None,gpu_busy_percent=None))
            sensor.write_text('unavailable');(device/'gpu_busy_percent').write_text('101')
            self.assertEqual(soak.gpu_sample({'0000:03:00.0'},root),dict(gpu_power_uw=None,gpu_busy_percent=None))

    def test_shutdown_cannot_hide_a_fault_after_a_completed_soak(self):
        stopped='Acceptance: stopping isolated processes\n'
        self.assertTrue(all(fault_checks(stopped+'ErrorSurfaceLostKHR').values()))
        for fault in ('Host fault', 'Guest fault', 'Assertion Failed',
                      'ErrorDeviceLost', 'Device lost while waiting', 'Fault (signal 11) at RIP 0x240'):
            self.assertFalse(all(fault_checks(stopped+fault).values()))
        self.assertFalse(all(fault_checks('STOP: abort\n'+stopped).values()))

    def test_tail_preserves_split_acknowledgements(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'log'; tail=soak.Tail(path)
            self.assertEqual(tail.lines(),[])
            path.write_text('Acceptance comm')
            self.assertEqual(tail.lines(),[])
            with path.open('a') as file: file.write('and: 1 warped\nnext\npart')
            self.assertEqual(tail.lines(),['Acceptance command: 1 warped','next'])
            self.assertEqual(tail.lines(),[])
            with path.open('a') as file: file.write('ial\n')
            self.assertEqual(tail.lines(),['partial'])

    def test_floor_requires_evidence(self):
        self.assertFalse(all(soak.floor_checks([]).values()))
        self.assertFalse(all(soak.floor_checks([dict(rss=1,vram=1,staging=1)]).values()))

    def test_reclamation_and_independent_growth_limits(self):
        floor=dict(rss=100<<20,vram=100<<20,staging=100<<20)
        lower={key:value//2 for key,value in floor.items()}
        self.assertTrue(all(soak.floor_checks([floor,lower]).values()))
        for key,limit in [('rss',256),('vram',512),('staging',32)]:
            final=dict(floor);final[key]+=(limit<<20)+1
            checks=soak.floor_checks([floor,final])
            self.assertFalse(checks[key])
            self.assertEqual(sum(not value for value in checks.values()),1)

    def test_hashes_cover_save_changes(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);(root/'slot').write_bytes(b'original')
            before=soak.hashes(root)
            self.assertEqual(before,soak.hashes(root))
            (root/'slot').write_bytes(b'changed')
            self.assertNotEqual(before,soak.hashes(root))

if __name__=='__main__': unittest.main()
