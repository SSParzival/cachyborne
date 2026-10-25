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
bash -n build.sh run.sh cachyborne-install.sh cachyborne-setup.sh cachyborne-launch.sh cachyborne-gui launcher/bb-launcher.sh scripts/check-all.sh
python3 -m py_compile launcher/cachyborne.py launcher/cachyborne_gui.py launcher/cachyborne_original.py launcher/bbport_launcher.py
bash build.sh --test
cmake --build out/gpu --target cachyborne-tests --parallel "${BB_BUILD_JOBS:-4}"
ctest --test-dir out/gpu --output-on-failure -L cachyborne
python3 -m unittest discover -s tests -p 'test_*.py'
if command -v spirv-val >/dev/null; then
    for shader in out/gpu/test-output/motion-shader-test/*.spv; do
        spirv-val --target-env vulkan1.3 "$shader"
    done
fi
out/bb-probe --vulkan-only
