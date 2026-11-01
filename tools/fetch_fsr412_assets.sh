#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Fetch the verified official AMD SDK 2.4 upscaler and build native Vulkan replay assets.
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
data=${XDG_DATA_HOME:-$HOME/.local/share}/cachyborne
sdk=$data/fsr-sdk-2.4.0
output=${1:-$data/fsr4_412}
mkdir -p "$sdk"
python3 - "$sdk" <<'PY'
from pathlib import Path
import hashlib
import os
import sys
import tempfile
import urllib.request

destination = Path(sys.argv[1])
name = 'amd_fidelityfx_upscaler_dx12.dll'
expected = 'aad4fccccb29084a655f9a4f9f613bd22c8c03fb4a0f8840148cc6543b4ccace'
url = ('https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/'
       'FidelityFX-SDK/v2.4.0/Kits/FidelityFX/signedbin/' + name)
target = destination / name
def checksum(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()
if not target.is_file() or checksum(target) != expected:
    fd, temporary = tempfile.mkstemp(prefix='.download-', dir=destination)
    try:
        with os.fdopen(fd, 'wb') as out, urllib.request.urlopen(url, timeout=90) as source:
            import shutil
            shutil.copyfileobj(source, out)
        with open(temporary, 'rb') as stream:
            if hashlib.file_digest(stream, 'sha256').hexdigest() != expected:
                raise ValueError('The AMD SDK upscaler checksum does not match; keeping the old file')
        os.chmod(temporary, 0o644)
        os.replace(temporary, target)
    finally:
        Path(temporary).unlink(missing_ok=True)
print('Verified official AMD FSR 4.1.2.4555:', target)
PY
python3 "$root/tools/fsr4cap/dll_info.py" "$sdk/amd_fidelityfx_upscaler_dx12.dll"
export BB_FSR411_OUT="$output"
export BB_FSR4CAP_WORK=${BB_FSR4CAP_WORK:-$root/out/fsr4cap-sdk-2.4.0}
if [[ -z ${BB_FSR4CAP_TOOLS:-} && -x $root/out/fsr4cap/dxil-spirv/build/dxil-spirv &&
      -f $root/out/fsr4cap/fsr4cap.exe ]]; then
    export BB_FSR4CAP_TOOLS=$root/out/fsr4cap
fi
bash "$root/tools/fsr4cap/build_assets.sh" "$sdk/amd_fidelityfx_upscaler_dx12.dll"
python3 - "$output" <<'PY'
from pathlib import Path
import json
import sys
Path(sys.argv[1], 'source.json').write_text(json.dumps({
    'sdk': '2.4.0', 'upscaler': '4.1.2.4555', 'backend': 'native Vulkan replay',
    'source': 'GPUOpen-LibrariesAndSDKs/FidelityFX-SDK v2.4.0',
    'sha256': 'aad4fccccb29084a655f9a4f9f613bd22c8c03fb4a0f8840148cc6543b4ccace'
}, indent=2) + '\n')
PY
echo "FSR 4.1.2 assets installed in $output; select this folder in Midnight."
