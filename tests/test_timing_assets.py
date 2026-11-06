"""Retail Bloodletter correction boundaries, player priority and publication."""
from paths import ROOT
import hashlib
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

import mods
import param_tables as params
import bloodletter_assets_fixture as timing


class BloodletterAssetsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.game = ROOT/'CUSA03173'
        source = cls.game/timing.PARAM_PATH
        if not source.is_file():
            raise unittest.SkipTest('Retail parameters not available')
        cls.source_hash = hashlib.sha256(source.read_bytes()).digest()
        cls.raw, cls.layout = params.load_table_data(cls.game, timing.TABLE)

    def test_retail_patch_changes_only_the_original_radius_word(self):
        changed, _ = timing.bloodletter_patch(self.raw, self.layout)
        original = dict(params.table_rows(self.raw, self.layout))
        result = dict(params.table_rows(changed, self.layout))
        self.assertEqual(set(result), set(original))
        self.assertEqual(sum(a != b for a, b in zip(self.raw, changed)), 4)
        for key, row in original.items():
            if key == timing.ROW:
                expected = row | {timing.FIELD: struct.unpack('<f', bytes.fromhex('9a99193f'))[0]}
                self.assertEqual(result[key], expected)
            else:
                self.assertEqual(result[key], row)
        self.assertIsNone(timing.bloodletter_patch(changed, self.layout))
        custom = params.apply_overrides(self.raw, self.layout,
                                       {'row': {str(timing.ROW): {timing.FIELD: 0.8}}})
        self.assertIsNone(timing.bloodletter_patch(custom, self.layout))

    def test_archive_neighbors_original_and_player_priority_are_preserved(self):
        original = params.archive(self.game/timing.PARAM_PATH)
        with tempfile.TemporaryDirectory() as folder:
            out = Path(folder)
            layer = timing.build_bloodletter_mod(self.game, out/'generated')
            result = params.archive(layer/timing.PARAM_PATH)
            self.assertEqual(set(result), set(original))
            for name in original:
                if name != timing.TABLE+'.param':
                    self.assertEqual(result[name], original[name])
            player = out/'player'
            params.build_mod(self.game,timing.TABLE,
                             {'row': {str(timing.ROW): {timing.FIELD: 0.8}}},player)
            overlay = mods.build_overlay(self.game,out/'overlay',[
                ('Cachyborne timing',layer),('Player attack settings',player)])
            self.assertEqual((overlay/timing.PARAM_PATH).read_bytes(),
                             (player/timing.PARAM_PATH).read_bytes())
            # A later preparation never repairs/replaces a user-edited layer.
            (layer/timing.PARAM_PATH).write_bytes(b'edited layer')
            second = timing.build_bloodletter_mod(self.game,out/'generated')
            self.assertNotEqual(second,layer)
            self.assertEqual((layer/timing.PARAM_PATH).read_bytes(),b'edited layer')
            self.assertIn(timing.TABLE+'.param',params.archive(second/timing.PARAM_PATH))
        self.assertEqual(hashlib.sha256((self.game/timing.PARAM_PATH).read_bytes()).digest(),
                         self.source_hash)

    def test_failed_publication_cleans_only_owned_layer_and_refuses_the_dump(self):
        with tempfile.TemporaryDirectory() as folder:
            out = Path(folder)
            keep = out/'player-content';keep.mkdir();(keep/'keep').write_bytes(b'keep')
            with patch.object(timing,'build_mod',side_effect=ValueError('late failure')):
                with self.assertRaisesRegex(ValueError,'late failure'):
                    timing.build_bloodletter_mod(self.game,out)
            self.assertEqual(list(out.iterdir()),[keep])
            self.assertEqual((keep/'keep').read_bytes(),b'keep')
        with self.assertRaisesRegex(ValueError,'outside'):
            timing.build_bloodletter_mod(self.game,self.game/'invalid-generated-layer')
        self.assertFalse((self.game/'invalid-generated-layer').exists())


if __name__ == '__main__':
    unittest.main()
