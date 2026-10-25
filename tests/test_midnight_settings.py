"""Regression coverage for native GUI parity, draft persistence and real mod ingestion."""

from paths import ROOT
import ast
import copy
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import cachyborne as core
import cachyborne_options as options
import game_check
import mods


class MidnightSettingsTests(unittest.TestCase):
    def test_schema_covers_every_original_setting_and_binding(self):
        tree = ast.parse((ROOT / "launcher/bbport_launcher.py").read_text())
        assignments = {
            n.targets[0].id: n.value
            for n in tree.body
            if isinstance(n, ast.Assign) and isinstance(n.targets[0], ast.Name)
        }
        original = {ast.literal_eval(key) for key in assignments["DEFAULTS"].keys}
        self.assertEqual(set(original), set(options.LEGACY_DEFAULTS))
        bindings = ast.literal_eval(assignments["CONTROLS"])
        self.assertEqual(
            [(r[0], r[2:]) for r in bindings],
            [(r[0], r[2:]) for r in options.CONTROL_BINDINGS],
        )

    def test_draft_and_personal_default_save_even_with_invalid_form(self):
        with (
            tempfile.TemporaryDirectory() as directory,
            patch.object(core, "CONFIG", Path(directory)),
        ):
            s = copy.deepcopy(core.DEFAULTS) | dict(
                width="", output_res="incomplete", runtime_ini={"key.cross": "X"}
            )
            core.save_preferences(s, as_default=True)
            with self.assertRaises(ValueError):
                core.validate_settings(s)
            self.assertEqual(core.preferences()["width"], "")
            self.assertEqual(
                core.read_json(core.CONFIG / "defaults.json")["runtime_ini"][
                    "key.cross"
                ],
                "X",
            )
            self.assertEqual(
                (core.CONFIG / "settings.json").stat().st_mode & 0o777, 0o600
            )
            (core.CONFIG / "settings.json").unlink()
            self.assertEqual(core.preferences()["output_res"], "incomplete")

    def test_projection_uses_presence_flags_correctly_and_preserves_extra_settings(
        self,
    ):
        s = copy.deepcopy(core.DEFAULTS)
        s["original_settings"] = dict(
            language="20",
            mangohud=False,
            frame_stats=False,
            gpu_profile=False,
            save_log=False,
            vk_validation=False,
            readbacks="2",
            preupload="2",
            display="MSI",
            gpu="AMD",
            memory_model="old",
            extra_env='CUSTOM="a b"',
        )
        s["gpu_ids"] = {"AMD": "0"}
        s["runtime_env"] = {
            "MANGOHUD": "1",
            "BB_FRAME_STATS": "1",
            "BB_VK_VALIDATION": "1",
            "CUSTOM_OLD": "kept",
        }
        result = options.reconcile(s)
        env = result["runtime_env"]
        for flag in options.PRESENCE_FLAGS.values():
            self.assertNotIn(flag, env)
        self.assertEqual(env["BB_LANGUAGE"], "20")
        self.assertEqual(env["BB_READBACKS"], "2")
        self.assertEqual(env["BB_GPU_ID"], "0")
        self.assertEqual(env["CUSTOM"], "a b")
        self.assertEqual(env["BB_PC_MODEL"], "0")
        self.assertEqual(env["CUSTOM_OLD"], "kept")
        self.assertEqual(env["BB_FPS"], "uncap")
        self.assertEqual(env["BB_FPS_LIMIT"], "144")
        self.assertIn("MANGOHUD", s["runtime_env"])

    def test_144_profile_preserves_selected_fsr4_native_hdr_mods_and_bindings(self):
        s = copy.deepcopy(core.DEFAULTS) | dict(
            upscaler="fsr4",
            render_res="2560x1440",
            hdr=True,
            mod_selection={"order": ["Long mod name"], "disabled": []},
            runtime_ini={"preset": "0", "key.cross": "X", "model_lod": "2"},
        )
        result = core.performance_settings(s)
        for key in ("upscaler", "render_res", "hdr", "mod_selection"):
            self.assertEqual(result[key], s[key])
        self.assertEqual(result["runtime_ini"]["preset"], "0")
        self.assertEqual(result["runtime_ini"]["key.cross"], "X")
        self.assertEqual(result["fps_limit"], 144)
        self.assertFalse(result["ramdisk"])

    def test_fsr4_missing_and_corrupted_assets_are_actionable(self):
        with tempfile.TemporaryDirectory() as directory:
            problem = options.fsr4_problem(directory, "2560x1440", "0")
            self.assertIn("rcas.spv", problem)
            (Path(directory) / "rcas.spv").write_bytes(b"invalid SPIR-V")
            self.assertIn("dañado", options.fsr4_problem(directory, "2560x1440", "0"))
            self.assertIn(
                "inválido", options.fsr4_problem(directory, "2560x1440", "-1")
            )

    def test_real_setup_after_successful_dcx_scan_accepts_named_mods(self):
        for name in ["AB", "Performance Drawparams (Hidden AA + Hidden Motion Blur)"]:
            with self.subTest(name=name), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                game = root / "CUSA03173"
                game.mkdir()
                (game / "eboot.bin").write_bytes(b"eboot")
                asset = game / "dvdroot_ps4/chr/a.dcx"
                asset.parent.mkdir(parents=True)
                asset.write_bytes(b"base")
                modroot = root / "mods"
                mod = modroot / name / "dvdroot_ps4/chr/a.dcx"
                mod.parent.mkdir(parents=True)
                mod.write_bytes(b"modified")
                (root / "out").mkdir()
                (root / "out/bb-probe").touch()
                (root / "out/bb-probe").chmod(0o755)
                s = copy.deepcopy(core.DEFAULTS) | dict(
                    upscaler="fsr3",
                    mods_dir=str(modroot),
                    mod_selection={"order": [name], "disabled": []},
                    patch_selection={"enabled": ["x"], "disabled": ["y"]},
                )
                calls = []

                def tool(command, **kwargs):
                    calls.append(command)
                    if command[1].endswith("game_check.py"):
                        # Successful DCX scanner, as in the user's failing trace.
                        return subprocess.CompletedProcess(command, 0)
                    out = Path(command[command.index("--out") + 1])
                    for artifact in core.ARTIFACTS:
                        if artifact != "bbport.ini":
                            (out / artifact).write_bytes(b"prepared")
                    return subprocess.CompletedProcess(command, 0)

                with (
                    patch.object(core, "ROOT", root),
                    patch.object(core, "CONFIG", root / "config"),
                    patch.object(core, "DATA", root / "data"),
                    patch.object(core, "CACHE", root / "cache"),
                    patch.object(game_check, "image_sha256", return_value="hash"),
                    patch.object(game_check, "problem", return_value=None),
                    patch.object(core.subprocess, "run", side_effect=tool),
                ):
                    profile = core.setup(game, s)
                    self.assertEqual(
                        (Path(profile["game"]) / "dvdroot_ps4/chr/a.dcx").read_bytes(),
                        b"modified",
                    )
                    self.assertEqual(asset.read_bytes(), b"base")
                    self.assertEqual(
                        core.load_profile()["generation"], profile["generation"]
                    )
                    patch_call = next(c for c in calls if c[1].endswith("patches.py"))
                    selection = Path(
                        patch_call[patch_call.index("--patches-config") + 1]
                    )
                    self.assertEqual(
                        json.loads(selection.read_text()), s["patch_selection"]
                    )

    def test_resolver_disabled_order_and_layer_shape(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            game = root / "game"
            game.mkdir()
            modroot = root / "mods"
            for name in ["AB", "A long mod name", "disabled"]:
                folder = modroot / name / "dvdroot_ps4"
                folder.mkdir(parents=True)
            layers = mods.layers_for_game(
                game,
                modroot,
                {"order": ["A long mod name", "AB"], "disabled": ["disabled"]},
            )
            self.assertEqual([n for n, _ in layers], ["A long mod name", "AB"])
            self.assertTrue(all(isinstance(path, Path) for _, path in layers))
            self.assertEqual(mods.layers_for_game(game, modroot, enabled=False), [])

    def test_legacy_null_binding_reset_becomes_explicit_original_value(self):
        settings = copy.deepcopy(core.DEFAULTS)
        settings["runtime_ini"].update({"key.cross": None, "pad.cross": None})
        result = options.reconcile(settings)
        self.assertEqual(result["runtime_ini"]["key.cross"], "F, Return")
        self.assertEqual(result["runtime_ini"]["pad.cross"], "a")
        self.assertIsNone(settings["runtime_ini"]["key.cross"])

    def test_online_projection_retains_both_pools_without_network_requests(self):
        for pool in ["seamless", "huntersdream"]:
            settings = copy.deepcopy(core.DEFAULTS)
            settings["original_settings"] = dict(
                online=True,
                online_pool=pool,
                online_bbsc=True,
                online_password=" secret ",
                thd_online_id="id",
                thd_token="token",
            )
            result = options.reconcile(settings)["runtime_env"]
            self.assertEqual(result["BB_ONLINE"], "1")
            if pool == "huntersdream":
                self.assertEqual(result["BB_THD_TOKEN"], "token")
                self.assertEqual(result["BB_NET_BACKEND"], "thd")
            else:
                self.assertEqual(result["BB_SHADNET_PASSWORD"], " secret ")
                self.assertEqual(result["BB_BBSC"], "1")

    @unittest.skipUnless(
        os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY"),
        "GTK display unavailable",
    )
    def test_actual_native_window_has_all_controls_and_saves_without_legacy_window(
        self,
    ):
        # Isolated settings and child GTK process; never closes/replaces the user's window.
        code = """
import copy, sys
sys.path.insert(0, 'launcher')
import cachyborne_gui as gui
app=gui.App(); app.register(None); app.css_provider=gui.apply_style()
s=copy.deepcopy(gui.core.DEFAULTS)
mods=gui.core.DATA/'mods'
for name in ('A','Long mod name'):
    (mods/name/'dvdroot_ps4').mkdir(parents=True)
patches=gui.core.DATA/'patches'; patches.mkdir()
(patches/'test.xml').write_text('<Patches><Metadata Name="Patch" AppVer="01.09" AppElf="eboot.bin"/></Patches>')
s.update(mods_dir=str(mods),patches_dir=str(patches))
gui.core.save_preferences(s)
w=gui.Preferences(app)
assert len(w.mod_rows)==2 and len(w.patch_rows)==1
w.move_mod('Long mod name',-1)
w.mod_rows[0][1].set_active(False)
w.patch_rows[0][1].set_active(True)
selection=w.content_selection()
assert selection['mod_selection']['order']==['Long mod name','A']
assert selection['mod_selection']['disabled']==['Long mod name']
assert selection['patch_selection']['enabled']==['test.xml/Patch']
assert gui.options.NATIVE_LEGACY_FIELDS <= {k[7:] for k in w.fields if k.startswith('legacy.')}
assert len(w.binding_defaults)==44
assert all('ini.'+key in w.fields for key in __import__('patches').EFFECTS)
assert len(app.get_windows())==1
w.widgets['upscaler'][0].set_selected(3)
w.widgets['ini.preset'][0].set_selected(0)
w.widgets['legacy.readbacks'][0].set_selected(2)
w.widgets['ini.key.cross'][0].set_text('X')
w.widgets['width'][0].set_text('')
assert w.save(as_default=True)
s=gui.core.preferences()
assert s['width']=='' and s['runtime_ini']['key.cross']=='X'
assert s['original_settings']['readbacks']=='2'
w.widgets['width'][0].set_text('2560')
assert w.save()
s=w.collect(validate=True)
assert s['upscaler']=='fsr4' and s['runtime_ini']['preset']=='0'
assert s['runtime_env']['BB_READBACKS']=='2'
assert s['mod_selection']==selection['mod_selection']
# Reset must survive reloading an older prepared INI containing a custom value.
prepared=gui.core.DATA/'prepared-test'; prepared.mkdir()
(prepared/'bbport.ini').write_text('key.cross=X\\n')
gui.core.atomic_json(gui.core.CONFIG/'game_profile.json',{'prepared':str(prepared)})
w.widgets['ini.key.cross'][0].set_text('F, Return')
w.autosave()
assert gui.core.preferences()['runtime_ini']['key.cross']=='F, Return'
assert 'bbport_launcher' not in sys.modules
w.destroy()
"""
        with tempfile.TemporaryDirectory() as directory:
            env = os.environ | {
                "XDG_CONFIG_HOME": directory + "/config",
                "XDG_DATA_HOME": directory + "/data",
                "PYTHONPATH": str(ROOT / "scripts"),
            }
            result = subprocess.run(
                [sys.executable, "-c", code],
                cwd=ROOT,
                env=env,
                capture_output=True,
                text=True,
                timeout=30,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
