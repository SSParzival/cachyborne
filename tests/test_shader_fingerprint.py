# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise production CMake identity generation, including real target usage requirements."""
from paths import ROOT
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


@unittest.skipUnless(all(shutil.which(tool) for tool in ('cmake', 'ninja', 'clang++')),
                     'CMake, Ninja and Clang required')
class ShaderFingerprintTests(unittest.TestCase):
    def project(self, folder, *, active='1', inactive='1', inherited='1', source_flag='1',
                emitter_flag='1', options='-fno-omit-frame-pointer', extra='', crlf=False,
                source_definition=None):
        source = Path(folder) / 'source tree'
        source.mkdir(exist_ok=True)
        for name in ('shader.cpp', 'emitter.cpp', 'unrelated.cpp'):
            path = source / name
            if not path.exists():
                path.write_bytes(b'int value() { return 1; }\r\n' if crlf else b'int value() { return 1; }\n')
        source_definition = source_definition if source_definition is not None else 'SOURCE_VARIANT=' + source_flag
        cmake = f'''cmake_minimum_required(VERSION 3.24)
project(IdentityFixture LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
include("{ROOT / 'gpu/cmake/ShaderFingerprint.cmake'}")
add_library(settings INTERFACE)
target_compile_definitions(settings INTERFACE INHERITED={inherited})
add_library(emitter STATIC EXCLUDE_FROM_ALL emitter.cpp)
target_compile_options(emitter PRIVATE "-DEMITTER={emitter_flag}")
add_library(translator STATIC EXCLUDE_FROM_ALL shader.cpp)
target_link_libraries(translator PRIVATE emitter settings)
target_compile_options(translator PRIVATE {options})
target_compile_definitions(translator PRIVATE
    "$<$<CONFIG:Release>:ACTIVE={active}>" "$<$<CONFIG:Debug>:INACTIVE={inactive}>"
    ROOT_LITERAL="${{CMAKE_CURRENT_SOURCE_DIR}}/assets/font")
set_source_files_properties(shader.cpp PROPERTIES COMPILE_DEFINITIONS {source_definition})
{extra}
bb_target_shader_fingerprint(translator "${{CMAKE_CURRENT_SOURCE_DIR}}"
    "${{CMAKE_CURRENT_BINARY_DIR}}/include/$<CONFIG>/bbgpu/shader_build.h"
    SOURCES "${{CMAKE_CURRENT_SOURCE_DIR}}/shader.cpp" "${{CMAKE_CURRENT_SOURCE_DIR}}/emitter.cpp"
    DEPENDENCY_TARGETS emitter)
'''
        (source / 'CMakeLists.txt').write_bytes(cmake.replace('\n', '\r\n').encode() if crlf else cmake.encode())
        return source

    def generate(self, source, *, config='Release', generator='Ninja', flags=''):
        build = source.parent / 'build tree'
        command = ['cmake', '-S', str(source), '-B', str(build), '-G', generator,
                   '-DCMAKE_CXX_COMPILER=' + shutil.which('clang++'), '-DCMAKE_CXX_FLAGS=' + flags]
        if generator == 'Ninja':
            command.append('-DCMAKE_BUILD_TYPE=' + config)
        subprocess.run(command, check=True, capture_output=True, text=True)
        subprocess.run(['cmake', '--build', str(build), '--config', config,
                        '--target', 'translator-shader-identity'], check=True, capture_output=True, text=True)
        path = build / 'include' / config / 'bbgpu/shader_build.h'
        return path.read_bytes(), path.stat().st_mtime_ns

    def test_test_targets_comments_and_inactive_conditions_preserve_header(self):
        with tempfile.TemporaryDirectory() as folder:
            source = self.project(folder)
            baseline = self.generate(source)
            self.project(folder, inactive='99', extra='''# Unrelated test registration
add_executable(extra-test EXCLUDE_FROM_ALL unrelated.cpp)
target_compile_definitions(extra-test PRIVATE TEST_ONLY=77)
enable_testing()
add_test(NAME extra COMMAND extra-test)
''')
            self.assertEqual(baseline, self.generate(source))

    def test_code_dependency_and_effective_compiler_settings_invalidate(self):
        with tempfile.TemporaryDirectory() as folder:
            source = self.project(folder)
            baseline = self.generate(source)[0]
            for values in ({'active': '2'}, {'inherited': '2'}, {'source_flag': '2'},
                           {'emitter_flag': '2'}, {'options': '-fno-omit-frame-pointer -ffast-math'},
                           {'extra': 'add_compile_definitions(DIRECTORY_VARIANT=2)'}):
                with self.subTest(values=values):
                    self.project(folder, **values)
                    self.assertNotEqual(baseline, self.generate(source)[0])
            self.project(folder)
            self.assertNotEqual(baseline, self.generate(source, flags='-fshort-enums')[0])
            self.generate(source)  # restore global flags
            for name in ('shader.cpp', 'emitter.cpp'):
                path = source / name
                original = path.read_bytes()
                path.write_bytes(b'int value() { return 2; }\n')
                self.assertNotEqual(baseline, self.generate(source)[0], name)
                path.write_bytes(original)
            self.project(folder, source_definition='OFF')
            false_like = self.generate(source)[0]
            self.project(folder, source_definition='FALSE')
            self.assertNotEqual(false_like, self.generate(source)[0])

    def test_relocation_and_line_endings_preserve_identity_and_mtime(self):
        with tempfile.TemporaryDirectory() as first, tempfile.TemporaryDirectory() as second:
            source = self.project(first)
            other = self.project(second, crlf=True)
            baseline = self.generate(source)
            self.assertEqual(baseline[0], self.generate(other)[0])
            self.assertEqual(baseline, self.generate(source))
            (source / 'shader.cpp').write_bytes(b'int value() { return 1; }\r\n')
            self.assertEqual(baseline, self.generate(source))

    def test_multi_config_headers_are_separate_and_use_active_settings(self):
        with tempfile.TemporaryDirectory() as folder:
            source = self.project(folder)
            release = self.generate(source, generator='Ninja Multi-Config')
            debug = self.generate(source, config='Debug', generator='Ninja Multi-Config')
            self.assertNotEqual(release[0], debug[0])
            self.project(folder, inactive='2')
            self.assertEqual(release, self.generate(source, generator='Ninja Multi-Config'))
            self.assertNotEqual(debug[0], self.generate(source, config='Debug', generator='Ninja Multi-Config')[0])
