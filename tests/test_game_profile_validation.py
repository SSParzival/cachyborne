"""Budgets and languages must be checked before accepting an unlaunchable profile."""

from paths import ROOT  # noqa: F401 - initialize repository import paths
import copy
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import cachyborne as core
import cachyborne_options as options


class GameProfileValidationTests(unittest.TestCase):
    def message_archives(self, root, folder, suffix=".msgbnd.dcx"):
        directory = root / "dvdroot_ps4/msg" / folder
        directory.mkdir(parents=True)
        for name in ("item", "menu"):
            (directory / (name + suffix)).write_bytes(b"data")

    def test_language_catalog_matches_all_retail_ps4_ids(self):
        self.assertEqual(
            set(options.LANGUAGE_FOLDERS), {code for code, _ in options.LANGUAGES}
        )
        self.assertEqual(options.LANGUAGE_FOLDERS["20"], "spaar")
        self.assertEqual(options.LANGUAGE_FOLDERS["3"], "spaes")

    def test_selector_excludes_missing_incomplete_empty_and_unknown_languages(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.message_archives(root, "spaar")
            self.message_archives(root, "engus", ".msgbnd")
            self.message_archives(root, "not-a-game-language")
            incomplete = root / "dvdroot_ps4/msg/frafr"
            incomplete.mkdir()
            (incomplete / "menu.msgbnd.dcx").write_bytes(b"menu")
            self.message_archives(root, "deude")
            (root / "dvdroot_ps4/msg/deude/item.msgbnd.dcx").write_bytes(b"")
            self.assertEqual(
                {code for code, _ in options.available_languages(root)}, {"1", "20"}
            )
            self.assertEqual(options.available_languages(""), [])

    def test_fresh_setup_defaults_to_spanish_from_installed_files(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.message_archives(root, "spaar")
            settings = copy.deepcopy(core.DEFAULTS)
            options.validate_game_language(settings, root)
            self.assertEqual(settings["runtime_env"]["BB_LANGUAGE"], "20")
            self.assertEqual(settings["runtime_env"]["BB_MENU_LANGUAGE"], "es")

    def test_spanish_variant_fallback_and_missing_nonspanish_rejection(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.message_archives(root, "spaes")
            settings = copy.deepcopy(core.DEFAULTS) | {
                "original_settings": {"language": "20"}
            }
            options.validate_game_language(settings, root)
            self.assertEqual(settings["runtime_env"]["BB_LANGUAGE"], "3")
            settings["original_settings"]["language"] = "8"
            with self.assertRaisesRegex(ValueError, "no está disponible"):
                options.validate_game_language(settings, root)

    def test_empty_message_tree_is_not_advertised_as_supported(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(ValueError, "No hay archivos"):
                options.validate_game_language(copy.deepcopy(core.DEFAULTS), directory)

    def test_native_pool_rejects_16gb_system_gtt_before_running_the_game(self):
        settings = copy.deepcopy(core.DEFAULTS) | {"render_res": "2560x1440"}
        with patch.object(
            core, "amd_gtt_budget", return_value=(8250814464, Path("/sys/example"))
        ):
            with self.assertRaisesRegex(ValueError, "1916x1078"):
                core.check_memory_budget(settings)
        self.assertEqual(core.direct_memory_mb(settings), 9152)

    def test_near_native_keeps_retail_pool_and_command_plan_consistent(self):
        settings = copy.deepcopy(core.DEFAULTS) | {"render_res": "1916x1078"}
        with patch.object(core, "amd_gtt_budget", return_value=(8250814464, None)):
            core.check_memory_budget(settings)
        self.assertEqual(core.direct_memory_mb(settings), 5056)

    def test_unknown_gtt_and_explicit_other_memory_backends_are_not_misclassified(self):
        settings = copy.deepcopy(core.DEFAULTS) | {"render_res": "2560x1440"}
        with patch.object(core, "amd_gtt_budget", return_value=(0, None)):
            core.check_memory_budget(settings)
        for key in ("BB_GUEST_VRAM", "BB_PC_MODEL", "BB_GUEST_IN_PLACE"):
            settings["runtime_env"] = {key: "1" if key == "BB_GUEST_VRAM" else "0"}
            with patch.object(core, "amd_gtt_budget", return_value=(1024, None)):
                core.check_memory_budget(settings)

    def test_sysfs_budget_only_uses_amd_cards_and_survives_missing_attributes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for card, vendor, budget in [
                ("card1", "0x1002", 8250814464),
                ("card2", "0x10de", 99999999999),
            ]:
                device = root / card / "device"
                device.mkdir(parents=True)
                (device / "vendor").write_text(vendor)
                (device / "mem_info_gtt_total").write_text(str(budget))
            self.assertEqual(core.amd_gtt_budget(root)[0], 8250814464)
            (root / "card1/device/mem_info_gtt_total").unlink()
            self.assertEqual(core.amd_gtt_budget(root), (0, None))


if __name__ == "__main__":
    unittest.main()
