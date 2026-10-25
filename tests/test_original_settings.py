"""Configuration parity with the original bbport editor, without network or game launch."""
from paths import ROOT
import copy
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(ROOT / 'launcher'))
import cachyborne as core
import cachyborne_original as bridge
import bbport_launcher as original


class OriginalSettingsTests(unittest.TestCase):
    def test_every_original_setting_and_ini_key_survives_import(self):
        settings = copy.deepcopy(original.DEFAULTS)
        settings.update(game_dir='/games/app0', user_dir='/games/saves', fps_mode='uncap', fps_limit=144,
                        mods_dir='/games/mods', draw_pipe='0', language='3')
        ini = dict(original.INI_DEFAULTS, upscaler='fsr3', preset='1', output_res='2560x1440',
                   **{'key.cross': 'X', 'pad.cross': 'b'})
        with tempfile.TemporaryDirectory() as directory, patch.object(bridge.core, 'DATA', Path(directory)), \
             patch.dict('os.environ', {}, clear=True):
            result = bridge.import_editor(copy.deepcopy(core.DEFAULTS), settings, ini)
        self.assertEqual(set(original.DEFAULTS), set(result['original_settings']))
        self.assertTrue(set(original.INI_DEFAULTS) <= set(result['runtime_ini']))
        self.assertEqual(result['runtime_ini']['key.cross'], 'X')
        self.assertEqual(result['runtime_ini']['pad.cross'], 'b')
        self.assertEqual(result['runtime_env']['BB_LANGUAGE'], '3')
        self.assertEqual(result['runtime_env']['BB_DRAW_PIPE'], '0')
        self.assertEqual(result['render_res'], '1706x960')
        self.assertEqual(result['fps'], '144')
        self.assertEqual(result['user_dir'], '/games/saves')

    def test_preparing_online_overrides_does_not_call_network_helpers(self):
        settings = copy.deepcopy(core.DEFAULTS)
        settings['runtime_env'] = {'BB_ONLINE': '1', 'BB_COMMUNITY_SERVER': 'https://example.invalid',
                                   'BB_SHADNET_WEBAPI': 'https://example.invalid/summon_messenger'}
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'out/gpu').mkdir(parents=True)
            (root / 'out/gpu/libbbnet20.so').touch()
            with patch.object(core, 'ROOT', root), patch('urllib.request.urlopen', side_effect=AssertionError('Network call during setup')):
                core.prepare_online_settings(settings, root / 'user')
            env = settings['runtime_env']
            self.assertTrue(Path(env['SHADPS4_HTTP_HOST_OVERRIDES_JSON']).is_file())
            self.assertEqual(env['BB_NET_MODULE'], str(root / 'out/gpu/libbbnet20.so'))
