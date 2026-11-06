# SPDX-License-Identifier: GPL-2.0-or-later
from paths import ROOT  # noqa: F401
import copy
import unittest
from unittest.mock import patch

from isolated_game import desktop_command, headless_command, isolate_profile


class PresentationContract(unittest.TestCase):
    def test_headless_removes_desktop_routes_and_uses_dummy_audio_after_wrapper(self):
        env = dict(DISPLAY=':1',WAYLAND_DISPLAY='wayland-1',WAYLAND_SOCKET='12',
                   GAMESCOPE_WAYLAND_DISPLAY='nested',BB_HDR='1',BB_FULLSCREEN='1',
                   SDL_AUDIO_DRIVER='pipewire',SDL_AUDIODRIVER='pipewire')
        original = env.copy()
        with patch('isolated_game.shutil.which',return_value='/gamescope'):
            command,result = headless_command(['probe','--arg'],env,audio=True)
        self.assertEqual(env,original)
        self.assertTrue({'DISPLAY','WAYLAND_DISPLAY','WAYLAND_SOCKET','GAMESCOPE_WAYLAND_DISPLAY'}.isdisjoint(result))
        self.assertEqual(result['SDL_VIDEO_DRIVER'],'wayland')
        self.assertEqual(result['SDL_AUDIO_DRIVER'],'dummy')
        self.assertEqual(command[-5:],['env','SDL_AUDIO_DRIVER=dummy','SDL_AUDIODRIVER=dummy','probe','--arg'])
        self.assertEqual(result['BB_HDR'],'0')

    def test_desktop_preserves_actual_route_monitor_and_hdr_but_stays_offline(self):
        env = dict(WAYLAND_DISPLAY='wayland-0',SDL_AUDIO_DRIVER='pipewire',
                   BB_DISPLAY='2',BB_HDR='1',BB_FULLSCREEN='1',BB_ONLINE='1',BB_BBSC='1')
        original = env.copy()
        command,result = desktop_command(['probe','--arg'],env,audio=True)
        self.assertEqual(env,original)
        self.assertEqual(command,['probe','--arg'])
        for key in ('WAYLAND_DISPLAY','SDL_AUDIO_DRIVER','BB_DISPLAY','BB_HDR','BB_FULLSCREEN'):
            self.assertEqual(result[key],env[key])
        self.assertEqual((result['BB_ONLINE'],result['BB_BBSC'],result['BB_AUDIO']),('0','0','auto'))

    def test_desktop_refuses_missing_or_dummy_devices(self):
        with self.assertRaisesRegex(RuntimeError,'active compositor'):
            desktop_command(['probe'],{})
        for key in ('SDL_VIDEO_DRIVER','SDL_VIDEODRIVER'):
            for driver in ('dummy','offscreen'):
                with self.assertRaisesRegex(RuntimeError,'video driver'):
                    desktop_command(['probe'],dict(DISPLAY=':1',**{key:driver}))
        for key in ('SDL_AUDIO_DRIVER','SDL_AUDIODRIVER'):
            for driver in ('dummy','disk','none'):
                with self.assertRaisesRegex(RuntimeError,'audio driver'):
                    desktop_command(['probe'],dict(DISPLAY=':1',**{key:driver}),audio=True)

    def test_desktop_keeps_presentation_choices_while_headless_isolates_them(self):
        original={'settings':dict(hdr=True,vrr=True,fullscreen=True,gamemode=True,gamescope=True,ramdisk=True)}
        desktop,headless=copy.deepcopy(original),copy.deepcopy(original)
        isolate_profile(desktop,audio=True,desktop=True)
        isolate_profile(headless,audio=False)
        for key in ('hdr','vrr','fullscreen'):
            self.assertTrue(desktop['settings'][key])
            self.assertFalse(headless['settings'][key])
        for key in ('gamemode','gamescope','ramdisk'):
            self.assertFalse(desktop['settings'][key])
            self.assertFalse(headless['settings'][key])


if __name__=='__main__':
    unittest.main()
