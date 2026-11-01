# SPDX-License-Identifier: GPL-2.0-or-later
"""Tests of evidence analysis, not claims of guest physics acceptance."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
SPEC=importlib.util.spec_from_file_location('physics',Path(__file__).resolve().parents[1]/'scripts/physics_acceptance.py')
physics=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(physics)

def path(rate,scale=1):
    return [(i/rate,352321536,(i/rate*scale,0,0)) for i in range(rate+1)]

class PhysicsEvidenceTests(unittest.TestCase):
    def test_sampling_independent_linear_motion(self):
        for rate in (60,144):
            self.assertTrue(physics.compare(path(30),path(rate),1)['horizontal_within_0_1_percent'])

    def test_unscaled_motion_fails(self):
        self.assertFalse(physics.compare(path(30),path(144,1.002),1)['horizontal_within_0_1_percent'])

    def test_stationary_trace_is_not_jump_evidence(self):
        with self.assertRaises(ValueError): physics.compare(path(30,0),path(60,0),1)

    def test_different_spawn_is_not_normalized_away(self):
        points=[(t,m,(x+0.002,y,z)) for t,m,(x,y,z) in path(60)]
        with self.assertRaises(ValueError): physics.compare(path(30),points,1)

    def test_missing_rates_rejected(self):
        with self.assertRaises(ValueError): physics.evaluate({'runs':[]},Path('.'))

    def test_reject_dropped_or_missing_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            file=Path(directory)/'trace.csv'
            file.write_text('ns,map,valid,loading,x,y,z,dropped\n0,1,1,0,0,0,0,1\n')
            with self.assertRaisesRegex(ValueError,'Dropped'): physics.trajectory(file,0,1)
            file.write_text('ns,map,valid,loading,x,y,z,dropped\n0,1,1,0,0,0,0,0\n')
            with self.assertRaisesRegex(ValueError,'cover'): physics.trajectory(file,0,1)

if __name__=='__main__': unittest.main()
