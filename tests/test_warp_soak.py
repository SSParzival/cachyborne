# SPDX-License-Identifier: GPL-2.0-or-later
import importlib.util
from pathlib import Path
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location('warp_soak', Path(__file__).resolve().parents[1] / 'scripts/stress_warp_soak.py')
soak = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(soak)

class WarpEvidenceTests(unittest.TestCase):
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
