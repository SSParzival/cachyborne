#!/usr/bin/env bash
# Complete project regression suite, including device-backed Vulkan tests.
set -euo pipefail
cd -- "$(dirname -- "$(readlink -f -- "$0")")/.."
local_prefix=$PWD/out/native-deps/usr
if [[ -x $local_prefix/bin/mold ]]; then
    export PATH="$local_prefix/bin:$PATH"
    export CMAKE_PREFIX_PATH="$local_prefix${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
    export LD_LIBRARY_PATH="$local_prefix/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    export LIBRARY_PATH="$local_prefix/lib${LIBRARY_PATH:+:$LIBRARY_PATH}"
    export CPATH="$local_prefix/include${CPATH:+:$CPATH}"
fi
export VK_LOADER_LAYERS_DISABLE=${VK_LOADER_LAYERS_DISABLE:-'~implicit~'}
bash -n build.sh run.sh cachyborne-install.sh cachyborne-setup.sh cachyborne-launch.sh cachyborne-gui launcher/bb-launcher.sh scripts/check-all.sh tools/debug_attach.sh
export PYTHONDONTWRITEBYTECODE=1
python3 -c 'import ast, pathlib, sys; [ast.parse(pathlib.Path(f).read_bytes(), filename=f) for f in sys.argv[1:]]' launcher/*.py scripts/*.py tools/debug_attach.py
python3 scripts/env_vars.py --check
bash build.sh --test
cmake --build out/gpu --target cachyborne-tests fsr4-bench --parallel "${BB_BUILD_JOBS:-4}"
ctest --test-dir out/gpu --output-on-failure -L cachyborne
python3 -m unittest discover -s tests -p 'test_*.py'
if command -v spirv-val >/dev/null; then
    for shader in out/gpu/test-output/motion-shader-test/*.spv; do
        spirv-val --target-env vulkan1.3 "$shader"
    done
    # Validate the exact production words, including the contact/PCSS shader.
    python3 - <<'SPIRV'
import pathlib, re, struct
header = pathlib.Path('out/gpu/include/bbgpu/shaders/contact_shadow_comp.h').read_text()
words = [int(word, 16) for word in re.findall(r'0x[0-9a-fA-F]+', header)]
assert words and words[0] == 0x07230203, 'Invalid production SPIR-V header'
pathlib.Path('out/gpu/test-output/contact-shadows-test/contact_shadow.spv').write_bytes(
    struct.pack('<' + 'I' * len(words), *words))
SPIRV
    spirv-val --target-env vulkan1.3 out/gpu/test-output/contact-shadows-test/contact_shadow.spv
fi
out/bb-probe --vulkan-only
