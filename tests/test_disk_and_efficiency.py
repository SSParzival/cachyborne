"""Unit tests for disk-space management and new efficiency option projections."""
from paths import ROOT
import copy
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(ROOT / 'launcher'))
sys.path.insert(0, str(ROOT / 'tests'))
import cachyborne as core
import cachyborne_options as options
from test_prepare import fixture
from test_game_check import param_sfo


def _fake_game(root: Path):
    (root / 'sce_sys').mkdir(parents=True, exist_ok=True)
    (root / 'sce_sys/param.sfo').write_bytes(param_sfo({'APP_VER': '01.09', 'TITLE_ID': 'CUSA03173'}))
    (root / 'eboot.bin').write_bytes(fixture())
    msg = root / 'dvdroot_ps4/msg/spaar'
    msg.mkdir(parents=True, exist_ok=True)
    (msg / 'item.msgbnd').write_bytes(b'x')
    (msg / 'menu.msgbnd').write_bytes(b'x')


class DiskAndEfficiencyTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.base = Path(self.tmp.name)
        self.config = self.base / 'config'
        self.data = self.base / 'data'
        self.cache = self.base / 'cache'
        self.ram = self.base / 'ram'
        self.dist = self.base / 'dist'
        self.patches = [
            mock.patch.object(core, 'CONFIG', self.config),
            mock.patch.object(core, 'DATA', self.data),
            mock.patch.object(core, 'CACHE', self.cache),
            mock.patch.object(core, 'RAM_ROOT', self.ram),
            mock.patch.object(core, 'ROOT', self.base),
        ]
        for p in self.patches:
            p.start()

    def tearDown(self):
        for p in reversed(self.patches):
            p.stop()
        self.tmp.cleanup()

    def _make_generation(self, name, payload_size=64, mtime_offset=0):
        gen_dir = self.data / 'prepared' / name
        gen_dir.mkdir(parents=True, exist_ok=True)
        artifact = gen_dir / 'boot-linked.bin'
        artifact.write_bytes(b'x' * payload_size)
        (gen_dir / 'game_profile.json').write_text(json.dumps({
            'generation': name,
            'prepared': str(gen_dir),
            'settings': copy.deepcopy(core.DEFAULTS),
        }))
        ts = 1_700_000_000 + mtime_offset
        os.utime(gen_dir, (ts, ts))
        return gen_dir

    def test_prune_prepared_profiles_keeps_active_generation_and_respects_keep_count(self):
        old_a = self._make_generation('a' * 32, payload_size=100, mtime_offset=10)
        old_b = self._make_generation('b' * 32, payload_size=200, mtime_offset=20)
        active = self._make_generation('c' * 32, payload_size=300, mtime_offset=30)
        self.config.mkdir(parents=True, exist_ok=True)
        (self.config / 'game_profile.json').symlink_to(active / 'game_profile.json')

        removed, freed = core.prune_prepared_profiles(self.data / 'prepared', keep=1)
        self.assertEqual(removed, 2)
        self.assertGreaterEqual(freed, 300)
        self.assertTrue(active.exists())
        self.assertFalse(old_a.exists())
        self.assertFalse(old_b.exists())

    def test_prune_old_ramdisk_keeps_active_when_enabled_and_purges_all_when_disabled(self):
        gen_a = self.ram / ('a' * 32)
        gen_b = self.ram / ('b' * 32)
        gen_a.mkdir(parents=True, exist_ok=True)
        gen_b.mkdir(parents=True, exist_ok=True)
        (gen_a / 'eboot.bin').write_bytes(b'1234')
        (gen_b / 'eboot.bin').write_bytes(b'5678')
        (self.ram / 'stage.lock').write_text('')

        removed, freed = core.prune_old_ramdisk(self.ram, active_generation='b' * 32, keep_active=True)
        self.assertEqual((removed, freed), (1, 4))
        self.assertFalse(gen_a.exists())
        self.assertTrue(gen_b.exists())
        self.assertTrue((self.ram / 'stage.lock').is_file())

        removed2, freed2 = core.prune_old_ramdisk(self.ram, active_generation='b' * 32, keep_active=False)
        self.assertEqual((removed2, freed2), (1, 4))
        self.assertFalse(gen_b.exists())

    def test_prune_old_logs_groups_csv_and_log_by_session(self):
        logs = self.data / 'logs'
        logs.mkdir(parents=True, exist_ok=True)
        for idx in range(5):
            stem = f'20261114_10000{idx}_10{idx}'
            for ext in ('.log', '.frames.csv', '.readbacks.csv'):
                p = logs / f'{stem}{ext}'
                p.write_bytes(b'L' * 20)
                os.utime(p, (1000 + idx, 1000 + idx))
        removed, freed = core.prune_old_logs(logs, keep_groups=2)
        self.assertEqual(removed, 9)
        self.assertEqual(freed, 180)
        remaining = sorted(p.name for p in logs.iterdir())
        self.assertTrue(all(name.startswith(('20261114_100003', '20261114_100004')) for name in remaining))

    def test_reclaim_disk_space_preserves_saves_active_shader_cache_and_current_dist(self):
        active = self._make_generation('c' * 32, payload_size=50, mtime_offset=20)
        stale = self._make_generation('d' * 32, payload_size=120, mtime_offset=10)
        self.config.mkdir(parents=True, exist_ok=True)
        (self.config / 'game_profile.json').symlink_to(active / 'game_profile.json')

        user_dir = self.data / 'user'
        savedata = user_dir / 'savedata/CUSA03173'
        savedata.mkdir(parents=True, exist_ok=True)
        (savedata / 'userdata0000').write_bytes(b'SAVE')
        shader_cache = user_dir / 'shader_cache'
        (shader_cache / 'previous').mkdir(parents=True, exist_ok=True)
        (shader_cache / 'previous/old.spv').write_bytes(b'P' * 40)
        (shader_cache / 'active.spv').write_bytes(b'ACTIVE')
        (shader_cache / 'partial.tmp').write_bytes(b'T' * 25)

        (self.base / 'VERSION').write_text('0.8.7\n')
        self.dist.mkdir(parents=True, exist_ok=True)
        old_tar = self.dist / 'cachyborne-0.8.5-x86_64.tar.gz'
        cur_tar = self.dist / 'cachyborne-0.8.7-x86_64.tar.gz'
        old_tar.write_bytes(b'O' * 80)
        cur_tar.write_bytes(b'C' * 80)

        summary = core.disk_usage_summary(copy.deepcopy(core.DEFAULTS), dist_dir=self.dist)
        self.assertEqual(summary['prepared_reclaimable'], directory_size(stale))
        self.assertEqual(summary['cache_reclaimable'], 65)
        self.assertEqual(summary['dist_reclaimable'], 80)

        result = core.reclaim_disk_space(copy.deepcopy(core.DEFAULTS), dist_dir=self.dist)
        self.assertGreaterEqual(result['freed_bytes'], 120 + 65 + 80)
        self.assertFalse(stale.exists())
        self.assertTrue(active.exists())
        self.assertTrue((savedata / 'userdata0000').is_file())
        self.assertTrue((shader_cache / 'active.spv').is_file())
        self.assertFalse((shader_cache / 'previous').exists())
        self.assertFalse((shader_cache / 'partial.tmp').exists())
        self.assertFalse(old_tar.exists())
        self.assertTrue(cur_tar.exists())

    def test_efficiency_options_reconcile_launch_and_validate(self):
        s = copy.deepcopy(core.DEFAULTS) | dict(aniso='8', pipe_rest='always', cache_read_ahead=False)
        reconciled = options.reconcile(s)
        self.assertEqual(reconciled['runtime_env']['BB_ANISO'], '8')
        self.assertEqual(reconciled['runtime_env']['BB_PIPE_REST'], 'always')
        self.assertEqual(reconciled['runtime_env']['BB_CACHE_READ_AHEAD'], '0')

        gen = self._make_generation('e' * 32)
        profile = dict(version=1, generation='e' * 32, game='/game', prepared=str(gen),
                       probe='/probe', user=str(self.data / 'user'), cache=str(self.cache), settings=s)
        _cmd, env = core.launch_command(profile, base_env={})
        self.assertEqual(env['BB_ANISO'], '8')
        self.assertEqual(env['RADV_TEX_ANISO'], '8')
        self.assertEqual(env['BB_PIPE_REST'], 'always')
        self.assertEqual(env['BB_CACHE_READ_AHEAD'], '0')

        s_zero = copy.deepcopy(core.DEFAULTS) | dict(aniso='0', pipe_rest='auto', cache_read_ahead=True)
        _cmd2, env2 = core.launch_command(profile | dict(settings=s_zero), base_env={})
        self.assertEqual(env2['BB_ANISO'], '0')
        self.assertNotIn('RADV_TEX_ANISO', env2)
        self.assertNotIn('BB_PIPE_REST', env2)
        self.assertNotIn('BB_CACHE_READ_AHEAD', env2)

        for bad in ({'aniso': '32'}, {'pipe_rest': 'invalid'}, {'auto_prune': 'yes'}, {'cache_read_ahead': 1}):
            with self.assertRaises(ValueError):
                core.validate_settings(copy.deepcopy(core.DEFAULTS) | bad)

    def test_setup_auto_prune_removes_previous_prepared_generation(self):
        game = self.base / 'CUSA03173'
        _fake_game(game)
        (self.base / 'VERSION').write_text('0.8.7\n')
        out_bin = self.base / 'out'
        out_bin.mkdir(parents=True, exist_ok=True)
        (out_bin / 'boot.elf').write_bytes(b'\x7fELF' + b'\0' * 60)
        (out_bin / 'libkernel.so').write_bytes(b'\x7fELF' + b'\0' * 60)
        (out_bin / 'bb-probe').write_bytes(b'#!/bin/sh\n')
        (out_bin / 'bb-probe').chmod(0o755)
        (out_bin / 'bb-gpu-capabilities').write_bytes(b'#!/bin/sh\n')
        (out_bin / 'bb-gpu-capabilities').chmod(0o755)
        scripts = self.base / 'scripts'
        scripts.mkdir(parents=True, exist_ok=True)
        assets = self.base / 'assets/icons'
        assets.mkdir(parents=True, exist_ok=True)
        (assets / 'cachyborne.bmp').write_bytes(b'BM')

        settings = copy.deepcopy(core.DEFAULTS) | dict(
            runtime_env={'BB_GUEST_VRAM': '0'},
            auto_prune=True,
            upscaler='fsr3',
        )
        old_gen = self._make_generation('1' * 32, payload_size=50, mtime_offset=1)
        self.config.mkdir(parents=True, exist_ok=True)
        (self.config / 'game_profile.json').symlink_to(old_gen / 'game_profile.json')

        def fake_tool(command, **_kwargs):
            if '--out' in command:
                out = Path(command[command.index('--out') + 1])
                for artifact in core.ARTIFACTS:
                    if artifact != 'bbport.ini':
                        (out / artifact).write_bytes(b'prepared')
            return mock.Mock(returncode=0)

        sys.path.insert(0, str(ROOT / 'scripts'))
        import game_check
        with mock.patch.object(core, 'amd_gtt_budget', return_value=(16 * 1024**3, None)), \
             mock.patch.object(game_check, 'image_sha256', return_value='hash'), \
             mock.patch.object(game_check, 'problem', return_value=None), \
             mock.patch('subprocess.run', side_effect=fake_tool):
            profile = core.setup(str(game), settings)
        self.assertFalse(old_gen.exists())
        self.assertTrue(Path(profile['prepared']).is_dir())


def directory_size(path):
    return core.directory_bytes(path)
