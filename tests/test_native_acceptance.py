"""Reject title screens, fallback, generated frames, truncated routes and timing spikes."""
from paths import ROOT  # noqa: F401
import copy
import unittest
from native_acceptance import analyze, read_records


class NativeAcceptanceTests(unittest.TestCase):
    def test_incomplete_capture_cli_returns_failed_json(self):
        import json
        from pathlib import Path
        import subprocess
        import sys
        import tempfile
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            for manifest in (None, {}, {'run': {'fps':60}}):
                if manifest is not None:
                    (root/'manifest.json').write_text(json.dumps(manifest))
                result=subprocess.run([sys.executable,str(ROOT/'scripts/native_acceptance.py'),str(root)],
                                      capture_output=True,text=True)
                self.assertEqual(result.returncode,1,result.stderr)
                failed=json.loads(result.stdout)
                self.assertFalse(failed['passed'])
                self.assertTrue(failed['error'])

    def fixture(self):
        run=dict(replay_start_ns=1000000000,duration_seconds=10,fps=60,
                 render_res='2560x1440',output_res='2560x1440',upscaler='fsr411',
                 preset='0',frame_generation='off')
        frames=[dict(monotonic_ns=1000000000+round(i*1e9/60),frame_ms=1000/60)
                for i in range(1,601)]
        poses=[dict(ns=r['monotonic_ns'],map=402718720,valid=1,loading=0,dropped=0) for r in frames]
        log=('Upscaler: FSR 4 inputs 2560x1440 -> 2560x1440\n'
             'Upscaler: FSR 4.1 replay, FP8 t2160_m0 output 2560x1440\n'
             'Runtime: pad replay finished\n')
        return frames,poses,log,run

    def test_whole_valid_native_route_passes(self):
        result=analyze(*self.fixture(),True)
        self.assertTrue(result['passed'])
        self.assertAlmostEqual(result['measured_fps'],60)
        self.assertEqual(result['frames'],600)

    def test_area_measurement_requires_the_requested_map_for_the_entire_window(self):
        frames,poses,log,run=self.fixture()
        run['expected_map']=0x20000000
        self.assertFalse(analyze(frames,poses,log,run,True)['gates']['requested_area'])
        for row in poses: row['map']=run['expected_map']
        self.assertTrue(analyze(frames,poses,log,run,True)['passed'])
        poses[-1]['map']=0x18010000
        self.assertFalse(analyze(frames,poses,log,run,True)['gates']['requested_area'])

    def test_a_fast_tail_cannot_stand_in_for_the_full_route(self):
        frames,poses,log,run=self.fixture()
        result=analyze(frames[-60:],poses,log,run,True)
        self.assertFalse(result['passed'])
        self.assertFalse(result['gates']['complete_frame_window'])

    def test_missing_rows_cannot_hide_stalls_or_reduce_the_measured_rate(self):
        frames,poses,log,run=self.fixture()
        missing = frames[:100]+frames[103:]
        result=analyze(missing,poses,log,run,True)
        self.assertFalse(result['gates']['frame_timestamp_spacing'])
        self.assertFalse(result['passed'])
        sparse=[row for i,row in enumerate(frames) if i%30!=10]
        result=analyze(sparse,poses,log,run,True)
        self.assertFalse(result['gates']['average_fps'])

    def test_bad_timing_is_not_accepted_using_a_smooth_average(self):
        frames,poses,log,run=self.fixture()
        frames[100]['frame_ms']=100
        result=analyze(frames,poses,log,run,True)
        self.assertFalse(result['passed'])
        self.assertFalse(result['gates']['worst_frame_time'])
        frames[100]['frame_ms']=float('nan')
        with self.assertRaises(ValueError): analyze(frames,poses,log,run,True)

    def test_native_configuration_does_not_substitute_fsr_off_or_generation(self):
        for changes in (dict(upscaler='off'),dict(preset='1'),dict(render_res='1706x960'),
                        dict(frame_generation='fsr3')):
            frames,poses,log,run=self.fixture()
            run.update(changes)
            self.assertFalse(analyze(frames,poses,log,run,True)['gates']['native_configuration'])

    def test_actual_backend_and_gameplay_are_required(self):
        frames,poses,log,run=self.fixture()
        for bad_log in ('FSR 3.1 available',log+'Upscaler: falling back to FSR 3.1\n'):
            self.assertFalse(analyze(frames,poses,bad_log,run,True)['gates']['actual_fsr411_native_backend'])
        for changes in (dict(valid=0),dict(loading=1),dict(map=0),dict(dropped=1)):
            bad=copy.deepcopy(poses);bad[20].update(changes)
            self.assertFalse(analyze(frames,bad,log,run,True)['gates']['valid_gameplay_window'])
        self.assertFalse(analyze(frames,poses,log,run,False)['passed'])

    def test_old_relative_logs_and_nonmonotonic_timestamps_are_not_proof(self):
        frames,poses,log,run=self.fixture()
        frames[100]['monotonic_ns']=frames[99]['monotonic_ns']
        with self.assertRaises(ValueError): analyze(frames,poses,log,run,True)
        with self.assertRaises(KeyError): analyze([dict(t_s=1,frame_ms=16.7)],poses,log,run,True)

    def test_fault_before_shutdown_fails_even_with_good_frame_times(self):
        frames,poses,log,run=self.fixture()
        self.assertFalse(analyze(frames,poses,log+'ErrorDeviceLost',run,True)['passed'])
        self.assertFalse(analyze(frames,poses,log+'Fault (signal 11) at RIP 0x240, address 0x240',run,True)['gates']['runtime_faults_absent'])
        self.assertTrue(analyze(frames,poses,log+'Acceptance: stopping isolated processes\nErrorSurfaceLostKHR',run,True)['passed'])

    def test_truncated_stdio_tail_requires_proof_that_window_already_ended(self):
        frames,poses,log,run=self.fixture()
        for missing in (None,''):
            bad=dict(monotonic_ns=missing,frame_ms='16.7')
            with self.assertRaises(ValueError): analyze(frames[:200]+[bad],poses,log,run,True)
            complete=frames+[dict(monotonic_ns=12000000000,frame_ms=1000/60)]
            result=analyze(complete+[bad],poses,log,run,True)
            self.assertTrue(result['passed'])
            self.assertTrue(result['truncated_record_after_window'])

    def test_partial_numeric_tail_needs_newline_evidence_and_a_finished_window(self):
        import tempfile
        from pathlib import Path
        frames,poses,log,run=self.fixture()
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'frames.csv'
            def write(rows,tail):
                path.write_text('frame_ms,monotonic_ns\n'+''.join(
                    f"{r['frame_ms']},{r['monotonic_ns']}\n" for r in rows)+tail)
                return read_records(path)
            complete=frames+[dict(monotonic_ns=12000000000,frame_ms=1000/60)]
            # The last field may consist of valid digits cut in mid-timestamp.
            result=analyze(write(complete,'16.7,13'),poses,log,run,True)
            self.assertTrue(result['passed'])
            self.assertTrue(result['truncated_record_after_window'])
            with self.assertRaises(ValueError):
                analyze(write(frames[:200],'16.7,13'),poses,log,run,True)
            # A completed backwards timestamp is corruption, not a truncated tail.
            with self.assertRaises(ValueError):
                analyze(write(complete,'16.7,13\n'),poses,log,run,True)

    def test_pose_tail_also_requires_a_complete_record_after_the_window(self):
        frames,poses,log,run=self.fixture()
        bad=dict(ns='13',valid='1',loading='0',map='402718720',dropped=None,_record_complete=False)
        with self.assertRaises(ValueError):analyze(frames,poses[:200]+[bad],log,run,True)
        complete=poses+[dict(ns=12000000000,valid=1,loading=0,map=402718720,dropped=0)]
        result=analyze(frames,complete+[bad],log,run,True)
        self.assertTrue(result['passed'])
        self.assertTrue(result['truncated_pose_record_after_window'])

    def test_ten_minute_window_is_measured_in_full(self):
        _,_,log,run=self.fixture();run['duration_seconds']=600
        frames=[dict(monotonic_ns=1000000000+round(i*1e9/60),frame_ms=1000/60)
                for i in range(1,36001)]
        poses=[dict(ns=r['monotonic_ns'],map=402718720,valid=1,loading=0,dropped=0) for r in frames]
        result=analyze(frames,poses,log,run,True)
        self.assertTrue(result['passed'])
        self.assertEqual(result['frames'],36000)
        self.assertFalse(analyze(frames[-7200:],poses,log,run,True)['passed'])

    def test_shutdown_must_not_hide_worker_faults(self):
        frames,poses,log,run=self.fixture()
        stopped=log+'Acceptance: stopping isolated processes\n'
        for fault in ('Host fault in CopyFilterThread', 'Guest fault',
                      'Assertion Failed', 'ErrorDeviceLost', 'Fault (signal 11) at RIP 0x240, address 0x240'):
            with self.subTest(fault=fault):
                result=analyze(frames,poses,stopped+fault,run,True)
                self.assertTrue(result['gates']['runtime_faults_absent'])
                self.assertFalse(result['gates']['shutdown_faults_absent'])
                self.assertFalse(result['passed'])
