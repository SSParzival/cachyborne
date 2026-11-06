"""Native heaps must fit distinct host/GPU budgets and match generated patches."""
from paths import ROOT
import copy
import json
from pathlib import Path
import struct
import unittest
from unittest.mock import patch

import cachyborne as core
from bbport_graphics_memory import GRAPHICS_HEAPS, graphics_heap_sizes, graphics_vram_layout, validate_heap_patches
from isolated_game import headless_command, isolate_profile, MemoryWatch
import patches


class NativeVramTests(unittest.TestCase):
    def test_game_aborts_when_host_memory_or_pressure_budget_changes(self):
        with self.assertRaisesRegex(RuntimeError, '4 GiB'):
            MemoryWatch(lambda: 3<<30)
        with (patch('isolated_game.time.monotonic', side_effect=[0,6]),
              patch.object(MemoryWatch, 'stalled_us', side_effect=[100,2000000])):
            watch = MemoryWatch(lambda: 8<<30)
            with self.assertRaisesRegex(RuntimeError, '25%'):
                watch.check()
        with (patch('isolated_game.time.monotonic', side_effect=[0,6]),
              patch.object(MemoryWatch, 'stalled_us', side_effect=[100,200])):
            watch = MemoryWatch(lambda: 8<<30)
            watch.check()
            watch.available = lambda: 1<<30
            with self.assertRaisesRegex(RuntimeError, '2 GiB'):
                watch.check()
    def settings(self):
        s = copy.deepcopy(core.DEFAULTS)
        s.update(render_res='2560x1440', output_res='2560x1440', upscaler='off')
        s['runtime_ini'].update(preset='0', upscaler='off')
        s['runtime_env'] = {'BB_GUEST_VRAM': 'graphics'}
        return s

    def test_actual_retail_heap_patches_and_vram_range_agree(self):
        # Actual ELF table signatures must match, not a fabricated address map.
        elf = ROOT / 'out/bbhost-retail.elf'
        if not elf.exists():
            self.skipTest('retail ELF unavailable')
        for scene in ((1920,1080), (2560,1440), (3840,2160)):
            for full in (False, True):
                writes = patches.graphics_heap_writes(elf.read_bytes(), scene, full)
                self.assertIsNotNone(writes)
                sizes = tuple(struct.unpack('<Q', data)[0] for _, data in writes)
                self.assertEqual(sizes, graphics_heap_sizes(scene, full))
                ranges, count = graphics_vram_layout(dict(render_res=f'{scene[0]}x{scene[1]}', large_heaps=full))
                low, high = (int(part, 16) for part in ranges.split('-'))
                self.assertEqual(high-low, sum(sizes))
                self.assertEqual(count, sum(sizes))

    @patch.object(core, 'gpu_bar_budget', return_value=14 << 30)
    @patch.object(core, 'amd_gtt_budget', return_value=(8251027456, Path('/sys/example')))
    def test_native_1440_fits_split_budgets(self, *_):
        s = self.settings()
        self.assertEqual(core.direct_memory_mb(s), 5936)
        core.check_memory_budget(s)
        profile = dict(settings=s, prepared='/prepared', user='/user', game='/game', probe='/probe', cache='/cache')
        _, env = core.launch_command(profile, base_env={'BB_DMEM_MB':'5056'})
        self.assertEqual(env['BB_GUEST_VRAM_RANGES'], graphics_vram_layout(s)[0])
        self.assertEqual(env['BB_RENDER_RES'], '2560x1440')
        self.assertEqual(env['BB_DMEM_MB'], '5936')
        self.assertEqual(env['BB_GUEST_MEMORY'], 'dmabuf')
        self.assertEqual(env['BB_GUEST_LAZY'], '0')

    @patch.object(core, 'gpu_bar_budget', return_value=14 << 30)
    @patch.object(core, 'amd_gtt_budget', return_value=(8251027456, Path('/sys/example')))
    def test_private_heap_only_keeps_b_in_host_cached_memory(self, *_):
        s = self.settings()
        s['runtime_env']['BB_GUEST_VRAM'] = 'graphics-private'
        core.check_memory_budget(s)
        env = core.graphics_vram_environment(s, s['runtime_env'])
        self.assertEqual(env['BB_GUEST_VRAM_RANGES'], '42c00000-ecb00000')
        self.assertEqual(graphics_vram_layout(s, 'graphics-private')[1], 2719 << 20)

    def test_no_bypass_with_insufficient_or_unknown_bar_budget(self):
        for budget in (0, 6 << 30):
            with patch.object(core, 'gpu_bar_budget', return_value=budget):
                with self.assertRaisesRegex(ValueError, 'VRAM BAR'):
                    core.check_memory_budget(self.settings())

    def test_gtt_still_required_for_non_graphics_memory(self):
        with (patch.object(core, 'gpu_bar_budget', return_value=14 << 30),
              patch.object(core, 'amd_gtt_budget', return_value=(2 << 30, None))):
            with self.assertRaisesRegex(ValueError, 'GTT'):
                core.check_memory_budget(self.settings())

    def test_final_patch_table_rejects_stale_or_overlapping_custom_writes(self):
        s = self.settings()
        sizes = graphics_heap_sizes((2560,1440))
        entries = [(address-0x400000, struct.pack('<Q', size))
                   for (address, _, _), size in zip(GRAPHICS_HEAPS, sizes)]
        def blob(entries):
            return struct.pack('<8sQQ', b'BBPATCH2', 0x400000, len(entries)) + b''.join(
                struct.pack('<QQ', offset, len(data))+data for offset, data in entries)
        validate_heap_patches(blob(entries), s)
        invalid = [blob([]), blob(entries)[:-1], b'',
                   blob(entries+[(entries[0][0]+4, b'\xff')])]
        for data in invalid:
            with self.assertRaises(ValueError):
                validate_heap_patches(data, s)

    def test_missing_gtt_budget_is_not_treated_as_unlimited(self):
        with (patch.object(core, 'gpu_bar_budget', return_value=14<<30),
              patch.object(core, 'amd_gtt_budget', return_value=(0,None))):
            with self.assertRaisesRegex(ValueError, 'GTT'):
                core.check_memory_budget(self.settings())

    def test_incompatible_modes_and_stale_ranges_rejected(self):
        for overrides in ({'BB_GUEST_MEMORY':'host'}, {'BB_GUEST_LAZY':'1'},
                          {'BB_LAYER_MEMORY':'0'}, {'BB_GUEST_IN_PLACE':'0'},
                          {'BB_PC_MODEL':'0'}, {'BB_GUEST_VRAM_RANGES':'42c00000-109100000'}):
            s = self.settings()
            s['runtime_env'].update(overrides)
            with self.assertRaises(ValueError):
                core.check_memory_budget(s)

    def test_capability_query_failure_does_not_claim_available_vram(self):
        for response in ('not json', '{}', json.dumps(dict(vendor=0x10de, discrete=True, bar_budget=16<<30)),
                         json.dumps(dict(vendor=0x1002, discrete=False, bar_budget=16<<30))):
            with patch.object(core.subprocess, 'run') as run:
                run.return_value.stdout = response
                self.assertEqual(core.gpu_bar_budget({}), 0)

    def test_headless_never_inherits_desktop_or_audio_routes(self):
        profile = dict(settings=self.settings())
        isolate_profile(profile)
        self.assertFalse(profile['settings']['audio'])
        self.assertFalse(profile['settings']['gamescope'])
        env = dict(DISPLAY=':0', WAYLAND_DISPLAY='wayland-0', WAYLAND_SOCKET='42',
                   GAMESCOPE_WAYLAND_DISPLAY='outer', BB_AUDIO='auto')
        with patch('isolated_game.shutil.which', return_value='/bin/gamescope'):
            command, isolated = headless_command(['/probe'], env)
        self.assertIn('headless', command)
        self.assertEqual(command[-2:], ['--', '/probe'])
        self.assertFalse(set(env)-{'BB_AUDIO'} & isolated.keys())
        self.assertEqual(isolated['BB_AUDIO'], 'none')
        self.assertEqual(isolated['SDL_VIDEO_DRIVER'], 'wayland')
        self.assertEqual(env['DISPLAY'], ':0')
        # Audio validation still uses a dummy endpoint; an inherited Pulse/
        # PipeWire route must never make the isolated test play on the desktop.
        isolate_profile(profile,audio=True)
        self.assertTrue(profile['settings']['audio'])
        with patch('isolated_game.shutil.which',return_value='/bin/gamescope'):
            audio_command, audible = headless_command(['/probe'],env,audio=True)
        self.assertEqual(audible['BB_AUDIO'],'auto')
        self.assertEqual(audible['SDL_AUDIO_DRIVER'],'dummy')
        self.assertNotIn('WAYLAND_DISPLAY',audible)
        self.assertEqual(audio_command[-5:],['--','env','SDL_AUDIO_DRIVER=dummy',
                                             'SDL_AUDIODRIVER=dummy','/probe'])
        with patch('isolated_game.shutil.which', return_value=None):
            with self.assertRaises(RuntimeError):
                headless_command(['/probe'], env)
