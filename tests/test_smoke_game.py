from paths import ROOT
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    "smoke_game", ROOT / "scripts/smoke-game.py"
)
smoke = importlib.util.module_from_spec(spec)
spec.loader.exec_module(smoke)


class RealGameSmokeChecks(unittest.TestCase):
    def test_fps_and_nonblank_gpu_readback_are_both_required(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            with self.assertRaisesRegex(RuntimeError, "imagen GPU"):
                smoke.check_rendered_output("Frame stats: 144.0 FPS", root)
            frame = root / "f000_final_16x16_bgra.raw"
            frame.write_bytes(bytes(16 * 16 * 4))
            with self.assertRaisesRegex(RuntimeError, "imagen GPU"):
                smoke.check_rendered_output("Frame stats: 144.0 FPS", root)
            frame.write_bytes(bytes(v for p in range(256) for v in (p, p, p, 255)))
            self.assertEqual(
                smoke.check_rendered_output("Frame stats: 144.0 FPS", root), [144.0]
            )

    def test_device_lost_and_zero_presented_frames_fail(self):
        with tempfile.TemporaryDirectory() as folder:
            with self.assertRaisesRegex(RuntimeError, "fallo"):
                smoke.check_rendered_output(
                    "Device lost while waiting for the GPU", folder
                )
            with self.assertRaisesRegex(RuntimeError, "cuadros"):
                smoke.check_rendered_output("Setup completado.", folder)

    def test_truncated_gpu_readback_cannot_pass(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / "f000_final_2560x1440_bgra.raw").write_bytes(bytes(range(256)))
            with self.assertRaisesRegex(RuntimeError, "imagen GPU"):
                smoke.check_rendered_output("Frame stats: 144.0 FPS", root)
