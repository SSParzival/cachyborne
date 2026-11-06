# SPDX-License-Identifier: GPL-3.0-or-later
"""Standalone GPU test builds may lack the native runtime test executable."""
import pathlib
import subprocess
import sys

if not pathlib.Path(sys.argv[1]).is_file():
    print('Build the native runtime with build.sh --test for HLE integration')
    raise SystemExit(77)
raise SystemExit(subprocess.call([sys.argv[1], '--plugin-hle-module', *sys.argv[2:]]))
