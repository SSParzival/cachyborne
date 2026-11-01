"""FSR callbacks needed by the release must be reproducible from pinned source."""
from paths import ROOT
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


@unittest.skipUnless(shutil.which('git') and (ROOT / 'gpu/third_party/fsr-vulkan/.git').exists(),
                     'initialized FSR Git submodule required for patch reproducibility')
class FsrBackendPatchTests(unittest.TestCase):
    def test_clean_pinned_source_accepts_all_patches_and_each_is_idempotent(self):
        submodule = ROOT / 'gpu/third_party/fsr-vulkan'
        sources = ('src/ffx_vk_fsr4_v07.c',
                   'upstream/ffx-1.1.4/sdk/src/backends/vk/ffx_vk.cpp')
        patches = sorted((ROOT / 'gpu/patches/fsr-vulkan').glob('*.patch'))
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in sources:
                target = root / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(subprocess.check_output(['git', '-C', str(submodule), 'show', 'HEAD:' + name]))
            for patch in patches:
                result = subprocess.run(['git', '-C', str(root), 'apply', '--check', str(patch)],
                                        capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                subprocess.run(['git', '-C', str(root), 'apply', str(patch)], check=True)
            for patch in patches:
                result = subprocess.run(['git', '-C', str(root), 'apply', '--reverse', '--check', str(patch)],
                                        capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
            backend = (root / sources[1]).read_text()
            self.assertIn('bool (*ffxVkShaderOverride)', backend)
            self.assertIn('void (*ffxVkJobMark)', backend)
            self.assertIn('if (ffxVkShaderOverride)', backend)
            self.assertIn('if (ffxVkJobMark)', backend)
