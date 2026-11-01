"""Community ports: build identity, translations and opt-in fault diagnostics."""
from paths import ROOT
import ast
import copy
import os
from pathlib import Path
import selectors
import shutil
import subprocess
import tempfile
import time
import unittest
from unittest.mock import patch

import cachyborne as core
import cachyborne_options as options
import bbport_i18n as i18n
from test_probe import EXE, package


class CommunityPortTests(unittest.TestCase):
    def fingerprint(self, folder, contents, flags='-O3'):
        root = Path(folder) / 'source tree'
        root.mkdir(exist_ok=True)
        (root / 'shader.h').write_bytes(contents)
        output = Path(folder) / 'generated' / 'build.h'
        script = Path(folder) / 'generate.cmake'
        script.write_text(f'''include("{ROOT / 'gpu/cmake/ShaderFingerprint.cmake'}")
set(CMAKE_CXX_COMPILER_ID "Clang")
set(CMAKE_CXX_COMPILER_VERSION "test")
set(CMAKE_CXX_FLAGS "{flags}")
bb_shader_fingerprint("{root}" "{output}" "{root / 'shader.h'}")
''')
        subprocess.run(['cmake', '-P', str(script)], check=True, capture_output=True)
        return output.read_bytes(), output.stat().st_mtime_ns

    @unittest.skipUnless(shutil.which('cmake'), 'CMake required')
    def test_shader_build_identity_ignores_location_and_crlf_but_not_code_or_flags(self):
        with tempfile.TemporaryDirectory() as a, tempfile.TemporaryDirectory() as b:
            header, stamp = self.fingerprint(a, b'line1\nline2\n')
            other, _ = self.fingerprint(b, b'line1\r\nline2\r\n')
            self.assertEqual(header, other)
            self.assertEqual(self.fingerprint(a, b'line1\nline2\n')[1], stamp)
            self.assertNotEqual(header, self.fingerprint(a, b'changed\n')[0])
            self.assertNotEqual(header, self.fingerprint(b, b'line1\nline2\n', '-O2')[0])

    def test_reference_translations_have_texture_choices_and_no_duplicate_keys(self):
        keys = ['Texture quality', 'Large textures in VRAM', 'Full', 'Half',
                'Quarter', 'Half with 3 GB of VRAM or less',
                'Half as big per side', 'A quarter per side']
        for locale in ('EN', 'PT_BR', 'ZH_CN', 'FR_FR'):
            for key in keys:
                self.assertIn(key, getattr(i18n, locale))
        tree = ast.parse((ROOT / 'launcher/bbport_i18n.py').read_text())
        for node in tree.body:
            if isinstance(node, ast.Assign) and isinstance(node.value, ast.Dict):
                names = [ast.literal_eval(key) for key in node.value.keys]
                self.assertEqual(len(names), len(set(names)))

    def test_fault_wait_projects_and_persists_without_changing_legacy_schema(self):
        settings = copy.deepcopy(core.DEFAULTS)
        settings['runtime_env']['BB_FAULT_WAIT'] = '1'
        self.assertNotIn('BB_FAULT_WAIT', options.reconcile(settings)['runtime_env'])
        settings['fault_wait'] = True
        normalized = options.reconcile(settings)
        self.assertEqual(normalized['runtime_env']['BB_FAULT_WAIT'], '1')
        with tempfile.TemporaryDirectory() as folder, patch.object(core, 'CONFIG', Path(folder)):
            core.save_preferences(normalized, as_default=True)
            self.assertTrue(core.preferences()['fault_wait'])
        self.assertNotIn('fault_wait', options.LEGACY_DEFAULTS)
        for invalid in ('false', '1', 1, None, [], {}):
            settings['fault_wait'] = invalid
            with self.assertRaisesRegex(ValueError, 'booleano'):
                options.reconcile(settings)

    @unittest.skipUnless(EXE.is_file(), 'build probe first')
    def test_fault_wait_keeps_only_opted_in_faulting_process_alive(self):
        with tempfile.TemporaryDirectory() as folder:
            image = Path(folder) / 'fault.bin'
            image.write_bytes(package(b'\x0f\x0b'))
            process = subprocess.Popen([str(EXE), str(image), '--cpu-only'],
                                       env=os.environ | {'BB_FAULT_WAIT': '1'},
                                       stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
            try:
                selector = selectors.DefaultSelector()
                selector.register(process.stderr, selectors.EVENT_READ)
                output = b''
                deadline = time.monotonic() + 10
                while b'waiting for debugger' not in output and time.monotonic() < deadline:
                    if selector.select(timeout=.5):
                        chunk = os.read(process.stderr.fileno(), 4096)
                        if not chunk: break
                        output += chunk
                selector.close()
                self.assertIsNone(process.poll(), output.decode(errors='replace'))
                self.assertIn(f'tools/debug_attach.sh {process.pid}'.encode(), output)
            finally:
                process.kill()
                process.communicate(timeout=5)
            result = subprocess.run([str(EXE), str(image), '--cpu-only'],
                                    env=os.environ | {'BB_FAULT_WAIT': '0'},
                                    capture_output=True, timeout=10)
            self.assertEqual(result.returncode, 132)

    def test_debug_attach_requires_explicit_numeric_pid(self):
        for args in ([], ['bad;pid'], ['0'], ['1', '2']):
            result = subprocess.run(['bash', str(ROOT / 'tools/debug_attach.sh'), *args],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 2, result.stderr)
