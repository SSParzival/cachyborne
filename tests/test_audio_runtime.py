"""Exercise the real AudioOut PCM conversion and queue policy using SDL's dummy device."""
from paths import ROOT
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest


class AudioRuntimeTests(unittest.TestCase):
    def test_pcm_queue_and_pacing(self):
        flags = shlex.split(subprocess.check_output(
            ['pkg-config', '--cflags', '--libs', 'sdl3'], text=True))
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'audio-test'
            subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra',
                            '-Werror', '-pthread', '-I.', '-Isrc', 'tests/test_audio.c',
                            *flags, '-o', str(binary)], cwd=ROOT, check=True, capture_output=True)
            result = subprocess.run([str(binary)], cwd=ROOT, capture_output=True,
                                    text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
