#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")"
mkdir -p out
# A user installation can keep missing development packages in this private prefix.
local_prefix=$PWD/out/native-deps/usr
if [[ -x $local_prefix/bin/mold ]]; then
    export PATH="$local_prefix/bin:$PATH"
    export CMAKE_PREFIX_PATH="$local_prefix${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
    export LD_LIBRARY_PATH="$local_prefix/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    export LIBRARY_PATH="$local_prefix/lib${LIBRARY_PATH:+:$LIBRARY_PATH}"
    export CPATH="$local_prefix/include${CPATH:+:$CPATH}"
fi
[[ $(uname -s) == Linux ]] || { echo 'Cachyborne requires Linux.' >&2; exit 1; }
export CC=${CC:-clang} CXX=${CXX:-clang++}
for tool in "$CC" "$CXX" mold pkg-config cmake ninja; do
    command -v "$tool" >/dev/null || { echo "Missing $tool; run cachyborne-install.sh" >&2; exit 1; }
done
"$CC" --version | grep -qi clang || { echo 'Native build requires Clang.' >&2; exit 1; }
pkg-config --exists vulkan sdl3 || { echo 'Need Vulkan and SDL3 development packages.' >&2; exit 1; }
native=(-march=native -mtune=native -O3 -pipe -fno-plt -fno-semantic-interposition -fno-omit-frame-pointer)
link=(-fuse-ld=mold -Wl,-O3,--as-needed)
if [[ ${BB_LTO:-ON} == ON ]]; then native+=(-flto=thin); fi
read -r -a includes <<< "$(pkg-config --cflags vulkan sdl3)"
read -r -a libraries <<< "$(pkg-config --libs vulkan sdl3)"
# GPU library (shadPS4 video core + drivers), built by CMake into out/gpu/libbbgpu.so.
# Native Clang builds use ThinLTO. GCC PGO profiles are intentionally rejected.
pgo=${BB_PGO:-off}
[[ $pgo == off ]] || { echo 'Native Clang build: GCC PGO profiles are unsupported; use BB_PGO=off.' >&2; exit 1; }
mkdir -p pgo
# Submodules (git clone --recursive, or: git submodule update --init) and this port's changes
# to FSR-Vulkan (gpu/patches/fsr-vulkan), applied to its working tree once.
if [[ ! -f gpu/third_party/fsr-vulkan/CMakeLists.txt || ! -f gpu/third_party/imgui/imgui.h || ! -f third_party/LibAtrac9/C/src/decoder.c ]]; then
    git submodule update --init --recursive
fi
# Check all patches before applying; preserve any existing submodule work on conflict.
fsr_patches_apply() {
    for patch in gpu/patches/fsr-vulkan/*.patch; do
        if ! git -C gpu/third_party/fsr-vulkan apply --reverse --check "$PWD/$patch" 2>/dev/null; then
            git -C gpu/third_party/fsr-vulkan apply "$@" "$PWD/$patch" || return 1
        fi
    done
}
if ! fsr_patches_apply --check 2>/dev/null; then
    echo 'FSR-Vulkan patches conflict with the submodule working tree; inspect its changes before rebuilding.' >&2
    exit 1
fi
fsr_patches_apply
cmake -S gpu -B out/gpu -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" -DBB_PGO="$pgo" \
    -DBB_LTO="${BB_LTO:-ON}" > out/gpu-cmake.log
grep '^-- Online module' out/gpu-cmake.log || true
echo "GPU library: PGO $pgo, LTO ${BB_LTO:-ON}"
# A failed GPU build must stop here: an older libbbgpu.so would otherwise be used silently.
if ! ninja -C out/gpu -j "${BB_BUILD_JOBS:-4}" bbgpu $(grep -q "^build bbnet:" out/gpu/build.ninja && echo bbnet) $(grep -q "^build bbnet20:" out/gpu/build.ninja && echo bbnet20) > out/gpu-build.log 2>&1; then
    grep -v '^\[' out/gpu-build.log | tail -40 >&2
    echo 'GPU library build failed (full log: out/gpu-build.log)' >&2; exit 1
fi
# Optional DLSS (NVIDIA RTX): DLSS_SDK_ROOT=<github.com/NVIDIA/DLSS checkout> builds the bridge
# (gpu/dlss_bridge, the only code using NVIDIA's SDK) and puts it next to bb-probe with NVIDIA's
# libnvidia-ngx-dlss.so. Without them the DLSS upscaler is listed as unavailable.
if [[ -n ${DLSS_SDK_ROOT:-} ]]; then
    cmake -S gpu/dlss_bridge -B out/dlss-bridge -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DDLSS_SDK_ROOT="$DLSS_SDK_ROOT" >/dev/null
    ninja -C out/dlss-bridge >/dev/null
    rm -f out/libnvidia-ngx-dlss.so.*
    cp out/dlss-bridge/libbbport_dlss.so "$DLSS_SDK_ROOT"/lib/Linux_x86_64/rel/libnvidia-ngx-dlss.so.* out/
    cp "$DLSS_SDK_ROOT/LICENSE.txt" out/NVIDIA-DLSS-LICENSE.txt
    echo "DLSS bridge: out/libbbport_dlss.so"
fi
# $ORIGIN/gpu: packaged copies keep the library next to the binary without patching it.
gpu=(-Lout/gpu -lbbgpu -Wl,-rpath,'$ORIGIN/gpu' -Wl,-rpath,"$PWD/out/gpu" -rdynamic)
runtime=(src/runtime*.c)
# Third-party decoders: compiled once, without this project's -Werror policy.
atrac9=(third_party/LibAtrac9/C/src/*.c)
if [[ ! -f out/libatrac9.a || build.sh -nt out/libatrac9.a || -n $(find third_party/LibAtrac9/C/src -newer out/libatrac9.a -name '*.c') ]]; then
    rm -rf out/atrac9 && mkdir -p out/atrac9
    for source in "${atrac9[@]}"; do "$CC" -std=c99 "${native[@]}" -g -w -c "$source" -o "out/atrac9/$(basename "${source%.c}").o"; done
    ar rcs out/libatrac9.a out/atrac9/*.o
fi
"$CC" -std=c11 "${native[@]}" "${link[@]}" -g -Wall -Wextra -Werror -pthread -no-pie "${includes[@]}" -I. -Isrc src/probe.c "${runtime[@]}" src/vulkan_smoke.c out/libatrac9.a -lm "${gpu[@]}" "${libraries[@]}" -o out/bb-probe
echo "Built $PWD/out/bb-probe"
# GPU check for run.sh (live_resolution=auto) and the launcher's gamepad list (--gamepads).
"$CC" -std=c11 "${native[@]}" "${link[@]}" -Wall -Wextra -Werror "${includes[@]}" tools/gpu_capabilities.c "${libraries[@]}" -o out/bb-gpu-capabilities
if [[ ${1:-} == --test ]]; then
    "$CC" -std=c11 "${native[@]}" "${link[@]}" -g -Wall -Wextra -Werror -pthread "${includes[@]}" -I. -Isrc tests/test_pad.c "${libraries[@]}" -lm -o out/pad-test
    out/pad-test
    "$CC" -std=c11 "${native[@]}" "${link[@]}" -g -Wall -Wextra -Werror -pthread -I. -Isrc tests/test_runtime.c "${runtime[@]}" out/libatrac9.a -lm "${gpu[@]}" "${libraries[@]}" -o out/runtime-test
    out/runtime-test
    "$CC" -std=c11 "${native[@]}" "${link[@]}" -g -Wall -Wextra -Werror -pthread -Isrc tests/test_file_mods.c -o out/file-mods-test
    out/file-mods-test
    "$CC" -std=c11 "${native[@]}" "${link[@]}" -g -Wall -Wextra -Werror -pthread -I. -Isrc tests/test_sema.c "${runtime[@]}" out/libatrac9.a -lm "${gpu[@]}" "${libraries[@]}" -o out/sema-test
    out/sema-test
    "$CC" -std=c11 "${native[@]}" "${link[@]}" -g -Wall -Wextra -Werror -pthread -I. -Isrc tests/test_online.c "${runtime[@]}" out/libatrac9.a -lm "${gpu[@]}" "${libraries[@]}" -o out/online-test
    out/online-test
    "$CC" -std=c11 -D_GNU_SOURCE "${native[@]}" "${link[@]}" -g -Wall -Wextra -Werror -I. -Isrc tests/test_content.c src/runtime_content.c -o out/content-test
    out/content-test
    "$CC" -std=c11 "${native[@]}" "${link[@]}" -Wall -Wextra -Werror -pthread -I. tests/test_cachy_affinity.c -o out/cachy-affinity-test
    out/cachy-affinity-test
fi
