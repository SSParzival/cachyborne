"""Prepared launch contract: no ingestion, rebuild or re-verification at runtime."""
from paths import ROOT
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('cachyborne', ROOT / 'launcher/cachyborne.py')
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class PreparedLaunchTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.game = self.root / 'game with spaces'
        self.game.mkdir()
        (self.game / 'eboot.bin').write_bytes(b'game')
        self.out = self.root / 'prepared'
        self.out.mkdir()
        for name in runner.ARTIFACTS:
            (self.out / name).write_bytes(b'prepared')
        self.probe = self.root / 'probe'
        self.probe.write_text(f'#!{sys.executable}\nimport json,sys,os\n'
                              'print(json.dumps({"args":sys.argv[1:],"config":os.environ["BB_CONFIG"]}))\n'
                              'sys.exit(7)\n')
        self.probe.chmod(0o755)
        self.profile = dict(version=1, generation='a' * 32, game=str(self.game), prepared=str(self.out),
                            probe=str(self.probe), user=str(self.root / 'user'), cache=str(self.root / 'cache'),
                            settings=runner.DEFAULTS.copy())
        self.config = self.root / 'config/cachyborne'
        runner.atomic_json(self.config / 'game_profile.json', self.profile)

    def test_run_directly_executes_prepared_probe_and_propagates_exit(self):
        env = dict(os.environ, XDG_CONFIG_HOME=str(self.config.parent))
        result = subprocess.run([str(ROOT / 'run.sh'), '--', '--test-argument'], env=env,
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 7, result.stderr)
        data = json.loads(result.stdout)
        self.assertEqual(data['args'][-1], '--test-argument')
        self.assertEqual(data['config'], str(self.out / 'bbport.ini'))
        self.assertIn(str(self.game), data['args'])
        self.assertEqual((self.out / 'patches.bin').read_bytes(), b'prepared')

    def test_missing_artifact_refuses_launch(self):
        (self.out / 'content.bin').unlink()
        with patch.object(runner, 'CONFIG', self.config):
            with self.assertRaisesRegex(RuntimeError, 'content.bin'):
                runner.load_profile()

    def test_new_preferences_do_not_mutate_prepared_profile(self):
        runner.atomic_json(self.config / 'settings.json', dict(fps='165'))
        with patch.object(runner, 'CONFIG', self.config):
            self.assertEqual(runner.preferences()['fps'], '165')
            self.assertEqual(runner.load_profile()['settings']['fps'], '144')

    def test_wrappers_keep_argument_boundaries_and_environment_overrides(self):
        self.profile['settings'].update(gamemode=True, gamescope=True, hdr=True, vrr=True, fps='165')
        with patch.dict(os.environ, {'RADV_PERFTEST': 'custom'}, clear=True):
            command, env = runner.launch_command(self.profile)
        self.assertEqual(command[0], 'gamescope')
        self.assertIn('--hdr-enabled', command)
        self.assertIn('--adaptive-sync', command)
        self.assertEqual(command[command.index('--') + 1], 'gamemoderun')
        self.assertIn(str(self.game), command)
        self.assertEqual(env['RADV_PERFTEST'], 'custom')
        self.assertEqual(env['BB_FPS_LIMIT'], '165')

    def test_failed_setup_preserves_previous_profile(self):
        sys.path.insert(0, str(ROOT / 'scripts'))
        import game_check
        with patch.object(runner, 'CONFIG', self.config), patch.object(runner, 'DATA', self.root / 'data'), \
             patch.object(runner, 'ROOT', self.root), patch.object(game_check, 'image_sha256', return_value='hash'), \
             patch.object(game_check, 'problem', return_value=None), patch.object(runner.subprocess, 'run') as run:
            (self.root / 'out').mkdir()
            (self.root / 'out/bb-probe').touch()
            run.side_effect = [None, subprocess.CalledProcessError(1, 'prepare')]
            before = (self.config / 'game_profile.json').read_bytes()
            with self.assertRaises(subprocess.CalledProcessError):
                runner.setup(self.game, runner.DEFAULTS | {'upscaler': 'fsr3'})
            self.assertEqual((self.config / 'game_profile.json').read_bytes(), before)
            self.assertEqual(list((self.root / 'data/prepared').iterdir()), [])

    def test_dry_run_does_not_stage_or_start_process(self):
        self.profile['settings']['ramdisk'] = True
        runner.atomic_json(self.config / 'game_profile.json', self.profile)
        with patch.object(runner, 'CONFIG', self.config), patch.object(runner, 'stage') as stage, \
             patch.object(runner.os, 'execvpe') as execute, patch('builtins.print'):
            self.assertEqual(runner.main(['launch', '--dry-run']), 0)
            stage.assert_not_called()
            execute.assert_not_called()

    def test_private_staging_directory_rejects_symlink(self):
        path = self.root / 'link'
        path.symlink_to(self.game)
        with self.assertRaisesRegex(RuntimeError, 'Unsafe staging'):
            runner.private_dir(path)

    def test_desktop_handles_paths_with_spaces_and_percent(self):
        with patch.object(runner, 'ROOT', self.root / 'my % game'), \
             patch.dict(os.environ, {'XDG_DATA_HOME': str(self.root / 'xdg')}):
            path = runner.desktop()
        self.assertIn('Exec="', path.read_text())
        self.assertIn('my %% game/cachyborne-launch.sh"', path.read_text())

    def test_ram_staging_materializes_links_and_warm_launch_reuses_marker(self):
        asset = self.root / 'asset'
        asset.write_bytes(b'asset data')
        (self.game / 'linked-asset').symlink_to(asset)
        with patch.object(runner, 'RAM_ROOT', self.root / 'ram'), \
             patch.object(runner, 'available_memory', return_value=128 * 1024**3):
            staged = runner.stage(self.profile)
            self.assertEqual((staged / 'linked-asset').read_bytes(), b'asset data')
            self.assertFalse((staged / 'linked-asset').is_symlink())
            with patch.object(runner.subprocess, 'run') as copy, \
                 patch.object(runner.subprocess, 'check_output') as scan:
                self.assertEqual(runner.stage(self.profile), staged)
                copy.assert_not_called()
                scan.assert_not_called()

    def test_interrupted_ram_copy_never_publishes_completion(self):
        with patch.object(runner, 'RAM_ROOT', self.root / 'ram'), \
             patch.object(runner, 'available_memory', return_value=128 * 1024**3), \
             patch.object(runner.subprocess, 'check_output', return_value='8 game'), \
             patch.object(runner.subprocess, 'run', side_effect=subprocess.CalledProcessError(1, 'rsync')):
            with self.assertRaises(subprocess.CalledProcessError):
                runner.stage(self.profile)
        self.assertFalse((self.root / 'ram' / self.profile['generation'] / '.cachyborne-complete').exists())

    def test_low_memory_refuses_staging_before_copy(self):
        with patch.object(runner, 'RAM_ROOT', self.root / 'ram'), \
             patch.object(runner, 'available_memory', return_value=1), \
             patch.object(runner.subprocess, 'check_output', return_value='8 game'), \
             patch.object(runner.subprocess, 'run') as copy:
            with self.assertRaisesRegex(RuntimeError, 'Insufficient'):
                runner.stage(self.profile)
            copy.assert_not_called()

    def test_published_shell_plan_directly_executes_without_python(self):
        with patch.object(runner, 'CONFIG', self.config):
            runner.publish_profile(self.profile)
        self.assertTrue((self.config / 'game_profile.json').is_symlink())
        # No python3 executable is reachable through PATH. Probe uses an absolute shebang.
        tools = self.root / 'tools'
        tools.mkdir()
        import shutil
        for tool in ('bash', 'readlink'):
            (tools / tool).symlink_to(shutil.which(tool))
        env = dict(os.environ, XDG_CONFIG_HOME=str(self.config.parent), PATH=str(tools))
        result = subprocess.run([str(ROOT / 'cachyborne-launch.sh'), '--', '--custom'],
                                env=env, capture_output=True, text=True)
        self.assertEqual(result.returncode, 7, result.stderr)
        data = json.loads(result.stdout)
        self.assertEqual(data['args'][-1], '--custom')
        self.assertEqual(data['config'], str(self.out / 'bbport.ini'))

    def test_profile_publication_failure_keeps_old_profile(self):
        before = (self.config / 'game_profile.json').read_bytes()
        with patch.object(runner, 'CONFIG', self.config), \
             patch.object(runner.os, 'replace', side_effect=OSError('disk full')):
            with self.assertRaises(OSError):
                runner.publish_profile(self.profile)
        self.assertEqual((self.config / 'game_profile.json').read_bytes(), before)

    def test_16gb_profile_bounds_memory_and_disables_prefetch(self):
        command, env = runner.launch_command(self.profile, base_env={})
        self.assertFalse(self.profile['settings']['ramdisk'])
        self.assertEqual(self.profile['settings']['memory_profile'], '16gb')
        self.assertEqual(env['BB_DMEM_MB'], '5056')
        self.assertEqual(env['BB_PRESENT_MODE'], 'Mailbox')
        self.assertEqual(env['BB_MEMORY_WILLNEED'], '0')
        self.assertEqual(env['BB_ASYNC_PIPELINE_THREADS'], '2')
        self.assertEqual(env['BB_DMEM_EXTRA_MB'], '256')
        self.assertEqual(env['BB_RENDER_RESOURCE_CACHE_MB'], '256')
        self.assertEqual(env['MESA_SHADER_CACHE_MAX_SIZE'], '4G')

    def test_high_internal_resolution_retains_required_direct_pool(self):
        self.profile['settings']['render_res'] = '2560x1440'
        _, env = runner.launch_command(self.profile, base_env={})
        self.assertEqual(env['BB_DMEM_MB'], '9152')

    def test_desktop_shortcut_uses_xdg_desktop_and_is_executable(self):
        with patch.object(runner.subprocess, 'check_output', return_value=str(self.root / 'Escritorio')), \
             patch.object(runner.shutil, 'which', return_value='/usr/bin/xdg-user-dir'):
            path = runner.desktop(on_desktop=True)
        self.assertEqual(path.parent, self.root / 'Escritorio')
        self.assertTrue(os.access(path, os.X_OK))
        self.assertIn('cachyborne-launch.sh', path.read_text())

    def test_cpu_ranges_reject_reversed_and_out_of_bounds_indices(self):
        for value in ('3-1', '1024', '0-2048'):
            with self.subTest(value=value):
                settings = runner.DEFAULTS | {'p_cpus': value}
                with self.assertRaisesRegex(ValueError, 'CPU range'):
                    runner.validate_settings(settings)

    def test_shortcuts_include_separate_game_and_configuration_entries(self):
        icon = self.root / 'xdg/icons/hicolor/512x512/apps/cachyborne.png'
        icon.parent.mkdir(parents=True)
        icon.write_bytes(b'local icon')
        with patch.dict(os.environ, {'XDG_DATA_HOME': str(self.root / 'xdg')}), \
             patch.object(runner.subprocess, 'check_output', return_value=str(self.root / 'Escritorio')), \
             patch.object(runner.shutil, 'which', return_value='/usr/bin/xdg-user-dir'):
            paths = runner.install_shortcuts(on_desktop=True)
        self.assertEqual(len(paths), 4)
        self.assertTrue(all(os.access(path, os.X_OK) for path in paths))
        play = self.root / 'Escritorio/cachyborne-play.desktop'
        settings = self.root / 'Escritorio/cachyborne.desktop'
        self.assertIn(f'Icon={icon}', play.read_text())
        self.assertIn('cachyborne-launch.sh', play.read_text())
        self.assertIn('cachyborne-gui', settings.read_text())
        self.assertIn('Name=Cachyborne — Configuración', settings.read_text())

    def test_icon_download_failure_preserves_local_icon_and_installs_shortcuts(self):
        with patch.dict(os.environ, {'XDG_DATA_HOME': str(self.root / 'xdg')}):
            icon = runner.icon_path()
            icon.parent.mkdir(parents=True)
            icon.write_bytes(b'previous icon')
            with patch('urllib.request.urlopen', side_effect=OSError('offline')), patch('builtins.print'):
                paths = runner.install_shortcuts(download_icon=True)
            self.assertEqual(icon.read_bytes(), b'previous icon')
            self.assertEqual(len(paths), 2)

    def test_nonimage_download_never_overwrites_installed_icon(self):
        from unittest.mock import MagicMock
        response = MagicMock()
        response.__enter__.return_value.read.return_value = b'<html>error page</html>'
        with patch.dict(os.environ, {'XDG_DATA_HOME': str(self.root / 'xdg')}):
            icon = runner.icon_path()
            icon.parent.mkdir(parents=True)
            icon.write_bytes(b'previous icon')
            with patch('urllib.request.urlopen', return_value=response):
                with self.assertRaisesRegex(ValueError, 'Invalid downloaded icon'):
                    runner.download_desktop_icon()
            self.assertEqual(icon.read_bytes(), b'previous icon')

    def test_desktop_escaping_handles_shell_metacharacters_in_checkout_path(self):
        import shutil
        if not shutil.which('desktop-file-validate'):
            self.skipTest('desktop-file-validate unavailable')
        with patch.object(runner, 'ROOT', self.root / 'a \\path "quoted" $dollar `tick` %'), \
             patch.dict(os.environ, {'XDG_DATA_HOME': str(self.root / 'xdg')}):
            path = runner.desktop()
        result = subprocess.run(['desktop-file-validate', str(path)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_jpeg_from_png_url_is_saved_as_a_real_png_icon(self):
        try:
            import gi
            gi.require_version('GdkPixbuf', '2.0')
            from gi.repository import GdkPixbuf
        except (ImportError, ValueError):
            self.skipTest('GdkPixbuf bindings unavailable')
        from unittest.mock import MagicMock
        pixbuf = GdkPixbuf.Pixbuf.new(GdkPixbuf.Colorspace.RGB, False, 8, 1, 1)
        pixbuf.fill(0x223344ff)
        ok, jpeg = pixbuf.save_to_bufferv('jpeg', [], [])
        self.assertTrue(ok)
        response = MagicMock()
        response.__enter__.return_value.read.return_value = jpeg
        with patch.dict(os.environ, {'XDG_DATA_HOME': str(self.root / 'xdg')}), \
             patch('urllib.request.urlopen', return_value=response):
            path = runner.download_desktop_icon()
        self.assertEqual(path.read_bytes()[:8], b'\x89PNG\r\n\x1a\n')

    def test_recommended_144_profile_preserves_user_bindings_and_paths(self):
        settings = runner.DEFAULTS | {'mods_dir': '/mods', 'user_dir': '/saves',
                                     'runtime_ini': {'key.cross': 'X', 'mouse_sensitivity': '1.5'},
                                     'runtime_env': {'BB_FPS': '60', 'BB_LANGUAGE': '3'}}
        recommended = runner.performance_settings(settings)
        self.assertEqual(recommended['fps'], '144')
        self.assertEqual(recommended['render_res'], '1706x960')
        self.assertEqual(recommended['upscaler'], 'fsr4')
        self.assertEqual(recommended['runtime_ini']['preset'], '1')
        self.assertEqual(recommended['runtime_ini']['key.cross'], 'X')
        self.assertEqual(recommended['user_dir'], '/saves')
        self.assertNotIn('BB_FPS', recommended['runtime_env'])
        self.assertEqual(recommended['runtime_env']['BB_LANGUAGE'], '3')
        self.assertEqual(settings['runtime_env']['BB_FPS'], '60')

    def test_complete_editor_environment_reaches_prepared_launch(self):
        self.profile['settings']['runtime_env'] = {'BB_LANGUAGE': '3', 'BB_DISPLAY': 'MSI',
                                                   'BB_DRAW_PIPE': '0', 'BB_READBACKS': '2',
                                                   'BB_PC_MODEL': '0', 'MANGOHUD': '1'}
        _, env = runner.launch_command(self.profile, base_env={})
        self.assertEqual(env['BB_LANGUAGE'], '3')
        self.assertEqual(env['BB_DISPLAY'], 'MSI')
        self.assertEqual(env['BB_DRAW_PIPE'], '0')
        self.assertEqual(env['BB_GUEST_IN_PLACE'], '0')
        self.assertEqual(env['VK_LOADER_LAYERS_DISABLE'], '')

    def test_live_resolution_unsets_fixed_guest_dimensions(self):
        self.profile['settings']['runtime_ini'] = {'live_resolution': '1'}
        _, env = runner.launch_command(self.profile, base_env={})
        self.assertEqual(env['BB_RENDER_RES'], '')
        self.assertEqual(env['BB_OUTPUT_RES'], '')

    def test_runtime_settings_cannot_inject_ini_lines_or_shell_assignments(self):
        for key, value in [('runtime_env', {'BAD;name': 'value'}),
                           ('runtime_ini', {'key.cross': 'X\nupscaler=off'})]:
            settings = runner.DEFAULTS | {key: value}
            with self.assertRaises(ValueError):
                runner.validate_settings(settings)

    def test_dry_run_redacts_online_credentials(self):
        result = runner.public_environment({'BB_THD_TOKEN': 'private', 'BB_SHADNET_PASSWORD': 'password',
                                            'BB_FPS': '144'})
        self.assertEqual(result['BB_THD_TOKEN'], '[redacted]')
        self.assertEqual(result['BB_SHADNET_PASSWORD'], '[redacted]')
        self.assertEqual(result['BB_FPS'], '144')

    def test_in_game_ini_changes_are_loaded_without_discarding_bindings(self):
        (self.out / 'bbport.ini').write_text('upscaler=taa\noutput_res=1920x1080\nkey.cross=Z\nmouse_sensitivity=1.75\n')
        with patch.object(runner, 'CONFIG', self.config):
            settings = runner.preferences()
        self.assertEqual(settings['upscaler'], 'taa')
        self.assertEqual(settings['runtime_ini']['key.cross'], 'Z')
        self.assertEqual(settings['runtime_ini']['mouse_sensitivity'], '1.75')

    def test_advanced_validation_bypass_is_rejected_before_ingestion(self):
        settings = runner.DEFAULTS | {'runtime_env': {'BB_SKIP_GAME_CHECK': '1'}}
        with self.assertRaisesRegex(RuntimeError, 'validation'):
            runner.setup(self.game, settings)

    def test_newer_in_game_bindings_override_stale_editor_draft(self):
        runner.atomic_json(self.config / 'settings.json', {'runtime_ini': {'key.cross': 'X'}})
        (self.out / 'bbport.ini').write_text('key.cross=Z\n')
        os.utime(self.config / 'settings.json', ns=(1000, 1000))
        os.utime(self.out / 'bbport.ini', ns=(2000, 2000))
        with patch.object(runner, 'CONFIG', self.config):
            self.assertEqual(runner.preferences()['runtime_ini']['key.cross'], 'Z')
        os.utime(self.config / 'settings.json', ns=(3000, 3000))
        with patch.object(runner, 'CONFIG', self.config):
            self.assertEqual(runner.preferences()['runtime_ini']['key.cross'], 'X')
