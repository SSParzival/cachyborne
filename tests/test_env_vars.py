# SPDX-FileCopyrightText: Copyright 2026 bbport contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""The environment variable list (gpu/shim/bb_env_vars.def) against the code, and its document."""
from paths import ROOT
import contextlib
import io
import unittest

import env_vars


class EnvVarsTests(unittest.TestCase):
    def test_default_launch_uses_one_60fps_pacer_and_radv_filtering(self):
        import copy
        import cachyborne as core
        import cachyborne_options as options
        profile = dict(settings=options.reconcile(copy.deepcopy(core.DEFAULTS)),
                       prepared='/prepared', user='/user', game='/game', probe='/probe', cache='/cache')
        _, env = core.launch_command(profile, base_env={})
        expected = dict(BB_FPS='60', BB_FPS_LIMIT='60', BB_FRAME_HOOKS='1',
                        BB_VBLANK_HZ='1000', RADV_TEX_ANISO='16', BB_DMEM_MB='5936',
                        BB_CPU_PINNING='1', BB_P_CPUS='0-11', BB_E_CPUS='12-19')
        for key, value in expected.items():
            self.assertEqual(env[key], value, key)

    def test_shader_progress_line_is_opt_in(self):
        source = (ROOT / 'gpu/shim/bbport_overlay.cpp').read_text(encoding='utf-8')
        self.assertIn('bb_env_on("BB_SHADER_PROGRESS")', source)
        self.assertNotIn('bb_env_off("BB_SHADER_PROGRESS")', source)
        import copy
        import cachyborne as core
        import cachyborne_options as options
        profile = dict(settings=options.reconcile(copy.deepcopy(core.DEFAULTS)),
                       prepared='/prepared', user='/user', game='/game', probe='/probe', cache='/cache')
        _, env = core.launch_command(profile, base_env={})
        self.assertNotEqual(env.get('BB_SHADER_PROGRESS', '')[:1], '1')

    def test_list_matches_the_code(self):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            ok = env_vars.check()
        self.assertTrue(ok, out.getvalue())

    def test_document_is_current(self):
        self.assertEqual((ROOT / 'docs/ENVIRONMENT.md').read_text(encoding='utf-8'), env_vars.document(),
                         'run: python3 scripts/env_vars.py --doc')


if __name__ == '__main__':
    unittest.main()
