# SPDX-License-Identifier: GPL-2.0-or-later
from paths import ROOT
from pathlib import Path
import tempfile
import unittest
import sys
sys.path.insert(0, str(ROOT / 'scripts'))
from isolated_game import isolate_configuration


class IsolatedConfigurationTests(unittest.TestCase):
    def test_capture_writes_cannot_change_published_configuration(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            source = root / 'published.ini'
            source.write_text('output_res=2560x1440\n')
            output = root / 'capture'
            output.mkdir()
            environment = {'BB_CONFIG': str(source), 'BB_FPS': '60'}
            isolated = isolate_configuration(environment, output)
            self.assertEqual(environment['BB_CONFIG'], str(source))
            self.assertEqual(isolated['BB_FPS'], '60')
            local = Path(isolated['BB_CONFIG'])
            self.assertEqual(local.read_bytes(), source.read_bytes())
            local.write_text('output_res=3440x1440\n')
            self.assertEqual(source.read_text(), 'output_res=2560x1440\n')

    def test_existing_capture_source_and_symlink_are_never_overwritten(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            source = root / 'bbport.ini'
            source.write_text('preserved\n')
            with self.assertRaises(ValueError):
                isolate_configuration({'BB_CONFIG': str(source)}, root)
            output = root / 'capture'
            output.mkdir()
            local = output / 'bbport.ini'
            local.symlink_to(source)
            with self.assertRaises(ValueError):
                isolate_configuration({'BB_CONFIG': str(source)}, output)
            local.unlink()
            local.write_text('old evidence\n')
            with self.assertRaises(FileExistsError):
                isolate_configuration({'BB_CONFIG': str(source)}, output)
            self.assertEqual(local.read_text(), 'old evidence\n')
            self.assertEqual(source.read_text(), 'preserved\n')
