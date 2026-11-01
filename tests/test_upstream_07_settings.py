"""Upstream 0.7 configuration survives native persistence and prepared execution."""
from paths import ROOT
import ast
import copy
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import cachyborne as core
import cachyborne_options as options
import patches


class Upstream07SettingsTests(unittest.TestCase):
    def settings(self, **ini):
        s = copy.deepcopy(core.DEFAULTS)
        s['runtime_ini'].update(ini)
        return s

    def test_default_60fps_quality_profile_matches_schema_and_projection(self):
        s = options.reconcile(self.settings())
        self.assertEqual((s['fps'], s['fps_limit']), ('60', 60))
        expected = dict(texture_quality='full', vbao='1', anti_aliasing='smaa',
                        preset='1', sharpen='1', sharpness='0.35', light_grid_opt='1',
                        effect_ssao='1', effect_dynamic_shadows='1', effect_bloom='1',
                        effect_chromatic_aberration='0', effect_motion_blur='0', effect_dof='0')
        for key, value in expected.items():
            self.assertEqual(s['runtime_ini'][key], value, key)
            self.assertEqual(options.INI_DEFAULTS[key], value, key)
        schema = json.loads((ROOT / 'launcher/settings_schema.json').read_text())
        items = {item['key']: item for section in schema['sections'] for item in section['items']}
        for key in expected.keys() & items.keys():
            value = items[key]['default']
            normalized = str(int(value)) if isinstance(value, bool) else str(value)
            self.assertEqual(normalized, expected[key], key)
        self.assertEqual(items['fps_mode']['default'], '60')
        self.assertEqual(items['fps_limit']['default'], 60)
        self.assertEqual(patches.fps_patches('60', {'BB_FRAME_HOOKS': '1'}), [])
        self.assertEqual(core.direct_memory_mb(s), 5056)
        self.assertEqual(patches.PRESET_SCALES[int(s['runtime_ini']['preset'])], 1.5)
        self.assertEqual(patches.PRESET_SCALES[0], 1.0)

    def test_every_upstream_ini_default_has_a_native_schema_entry(self):
        tree = ast.parse((ROOT / 'launcher/bbport_launcher_common.py').read_text())
        nodes = [n for n in tree.body if isinstance(n, ast.Assign)
                 and isinstance(n.targets[0], ast.Name)
                 and n.targets[0].id in ('EFFECTS', 'INI_DEFAULTS')]
        namespace = {}
        exec(compile(ast.Module(body=nodes, type_ignores=[]), '<upstream defaults>', 'exec'), namespace)
        self.assertLessEqual(set(namespace['INI_DEFAULTS']), set(options.INI_DEFAULTS))
        for key in options.INI_OPTIONS:
            self.assertIn(options.INI_OPTIONS[key][1], ('graphics', 'performance', 'controls', 'advanced'))

    def test_new_ini_choices_roundtrip_personal_defaults_and_prepared_launch(self):
        for key, (_, _, _, choices) in options.INI_OPTIONS.items():
            for value in choices:
                with self.subTest(key=key, value=value), tempfile.TemporaryDirectory() as directory:
                    s = options.reconcile(self.settings(**{key: str(value)}))
                    with patch.object(core, 'CONFIG', Path(directory)):
                        core.save_preferences(s, as_default=True)
                        (core.CONFIG / 'settings.json').unlink()
                        loaded = core.preferences()
                    self.assertEqual(loaded['runtime_ini'][key], str(value))
                    profile = dict(settings=loaded, prepared=directory, user='/user',
                                   game='/game', probe='/probe', cache='/cache')
                    command, env = core.launch_command(profile, base_env={})
                    self.assertEqual(env['BB_CONFIG'], str(Path(directory) / 'bbport.ini'))
                    self.assertIn('--patches', command)

    def test_invalid_native_values_are_rejected_without_losing_drafts(self):
        for key in options.INI_OPTIONS:
            for value in ('invalid', None, 'nan', 'inf'):
                with self.subTest(key=key, value=value), tempfile.TemporaryDirectory() as directory:
                    s = self.settings(**{key: value})
                    with patch.object(core, 'CONFIG', Path(directory)):
                        core.save_preferences(s)
                        self.assertEqual(core.preferences()['runtime_ini'][key], value)
                    with self.assertRaises(ValueError):
                        core.validate_settings(s)

    def test_native_voice_codes_project_into_upstream_language_patches(self):
        for voice, code in options.VOICE_GAME_CODES.items():
            s = options.reconcile(self.settings() | dict(voice_language=voice))
            self.assertEqual(s['runtime_ini']['game_voice_language'], code)
            self.assertEqual(s['runtime_env']['BB_VOICE_LANGUAGE'], voice)
            self.assertEqual(s['runtime_env']['BB_LANGUAGE'], '20')

    def test_explicit_text_language_overrides_system_without_changing_voice(self):
        s = options.reconcile(self.settings(game_text_language='frfr'))
        self.assertEqual(s['runtime_env']['BB_LANGUAGE'], '2')
        self.assertEqual(s['runtime_env']['BB_VOICE_LANGUAGE'], 'eng')
        self.assertEqual(s['runtime_ini']['game_voice_language'], 'engb')

    def test_native_aa_uses_upstream_patch_semantics(self):
        self.assertEqual(patches.anti_aliasing({'anti_aliasing': 'auto', 'upscaler': 'fsr1'}), (False, False))
        self.assertEqual(patches.anti_aliasing({'anti_aliasing': 'fxaa', 'upscaler': 'off'}), (True, True))
        self.assertEqual(patches.anti_aliasing({'anti_aliasing': 'smaa', 'upscaler': 'off'}), (False, False))

    def test_trophy_key_validation_and_log_redaction(self):
        s = self.settings()
        s['original_settings'] = {'trophy_key': 'aB' * 16}
        result = options.reconcile(s)
        self.assertEqual(result['runtime_env']['BB_TROPHY_KEY'], 'aB' * 16)
        self.assertEqual(core.public_environment(result['runtime_env'])['BB_TROPHY_KEY'], '[redacted]')
        s['original_settings']['trophy_key'] = 'not-a-key'
        with self.assertRaisesRegex(ValueError, 'trofeos'):
            options.reconcile(s)

    def test_retired_memory_paths_cannot_reenter_from_saved_or_inherited_env(self):
        with tempfile.TemporaryDirectory() as directory, patch.object(core, 'CONFIG', Path(directory)):
            core.atomic_json(core.CONFIG / 'settings.json', {
                'original_settings': dict(readbacks='2', memory_model='old', as_0_3=True, preupload='2')})
            migrated = core.preferences()
            self.assertEqual(migrated['readbacks'], '2')
            self.assertFalse({'readbacks', 'memory_model', 'as_0_3', 'preupload'} & migrated['original_settings'].keys())
            migrated['readbacks'] = ''
            self.assertNotIn('BB_READBACKS', options.reconcile(migrated)['runtime_env'])
        s = self.settings()
        s['runtime_env'] = {key: '1' for key in options.RETIRED_ENV}
        s['original_settings'] = dict(memory_model='old', as_0_3=True, preupload='2', readbacks='2')
        s = options.reconcile(s)
        self.assertEqual(set(s['original_settings']), set(options.LEGACY_DEFAULTS))
        self.assertEqual(s['runtime_env']['BB_READBACKS'], '2')
        profile = dict(settings=s, prepared='/prepared', user='/user', game='/game', probe='/probe', cache='/cache')
        _, env = core.launch_command(profile, base_env={key: '1' for key in options.RETIRED_ENV})
        self.assertFalse(options.RETIRED_ENV & env.keys())
        self.assertEqual(env['BB_MEMORY_WILLNEED'], '0')
        self.assertEqual(env['BB_DMEM_EXTRA_MB'], '256')

    def test_monitor_auto_resolution_is_resolved_only_at_setup(self):
        rows = [['Other', '1920x1080', '0'], ['Main', '2560x1440', '1']]
        with patch.object(options, 'capabilities', return_value=rows):
            self.assertEqual(options.monitor_output(ROOT), '2560x1440')
            self.assertEqual(options.monitor_output(ROOT, 'Other'), '1920x1080')
            self.assertEqual(options.monitor_output(ROOT, '1'), '1920x1080')
        with patch.object(options, 'capabilities', return_value=[]):
            with self.assertRaisesRegex(ValueError, 'monitor'):
                options.monitor_output(ROOT)
        s = options.reconcile(self.settings() | dict(output_auto=True))
        self.assertEqual(s['runtime_ini']['output_res'], 'auto')
        profile = dict(settings=s, prepared='/prepared', user='/user', game='/game', probe='/probe', cache='/cache')
        with patch.object(options, 'capabilities', side_effect=AssertionError('display probe on launch')):
            _, env = core.launch_command(profile, base_env={})
        self.assertEqual(env['BB_MONITOR_SIZE'], '2560x1440')

    def test_texture_pack_large_heaps_cannot_bypass_16gb_budget(self):
        s = self.settings() | dict(large_heaps=True)
        self.assertEqual(core.direct_memory_mb(s), 9152)
        with self.assertRaisesRegex(ValueError, '16 GB'):
            core.check_memory_budget(s)

    def test_pre_07_frame_generation_boolean_is_migrated(self):
        with tempfile.TemporaryDirectory() as directory, patch.object(core, 'CONFIG', Path(directory)):
            core.atomic_json(core.CONFIG / 'settings.json', {
                'runtime_ini': dict(frame_generation='1', effect_game_aa='0')})
            loaded = core.preferences()['runtime_ini']
            self.assertEqual(loaded['frame_generation'], 'fsr3')
            self.assertEqual(loaded['anti_aliasing'], 'off')
