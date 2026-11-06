# bbport GPU library: shadPS4 video core + shader recompiler + Gnm/VideoOut
# drivers, adapted to the native_probe runtime through shims in shim/.
cmake_minimum_required(VERSION 3.24)
project(bbgpu LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()
# Frame pointers keep the loader's thread dumps (watchdog, SIGUSR2 sampling) walkable.
add_compile_options(-fno-omit-frame-pointer)

if (NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "Cachyborne targets Linux only")
endif()
if (NOT CMAKE_C_COMPILER_ID STREQUAL "Clang" OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR "Cachyborne requires Clang/LLVM")
endif()
find_program(BB_MOLD mold REQUIRED)
add_compile_options(-march=native -mtune=native -O3 -pipe -fno-plt -fno-semantic-interposition)
add_link_options(-fuse-ld=${BB_MOLD} -Wl,-O3,--as-needed)
option(BB_LTO "ThinLTO native build" ON)
set(BB_PGO "off" CACHE STRING "Native build supports off; GCC profiles are incompatible")
if (NOT BB_PGO STREQUAL "off")
    message(FATAL_ERROR "GCC PGO profiles cannot be used with the native Clang toolchain")
endif()
if (BB_LTO)
    add_compile_options(-flto=thin)
    add_link_options(-flto=thin)
endif()

find_package(Vulkan REQUIRED)
find_package(Threads REQUIRED)
find_package(fmt REQUIRED)
find_package(Boost REQUIRED)
find_package(magic_enum QUIET)
if(NOT magic_enum_FOUND)
    # Header-only dependency.
    include(FetchContent)
    FetchContent_Declare(magic_enum GIT_REPOSITORY https://github.com/Neargye/magic_enum.git GIT_TAG v0.9.7)
    FetchContent_MakeAvailable(magic_enum)
endif()
find_package(tsl-robin-map REQUIRED)
find_package(VulkanMemoryAllocator CONFIG QUIET)
if (NOT TARGET GPUOpen::VulkanMemoryAllocator)
    # VMA is header-only; current Arch repositories do not package it.
    find_path(VMA_INCLUDE_DIR vk_mem_alloc.h PATH_SUFFIXES vma)
    if (NOT VMA_INCLUDE_DIR)
        include(FetchContent)
        FetchContent_Declare(bb_vma GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git GIT_TAG v3.3.0)
        FetchContent_MakeAvailable(bb_vma)
        set(VMA_INCLUDE_DIR "${bb_vma_SOURCE_DIR}/include")
    endif()
    if (NOT TARGET GPUOpen::VulkanMemoryAllocator)
        add_library(GPUOpen::VulkanMemoryAllocator INTERFACE IMPORTED)
        target_include_directories(GPUOpen::VulkanMemoryAllocator INTERFACE ${VMA_INCLUDE_DIR})
    endif()
endif()
find_package(PkgConfig REQUIRED)
pkg_check_modules(XXHASH REQUIRED IMPORTED_TARGET libxxhash)
pkg_check_modules(LIBDEFLATE REQUIRED IMPORTED_TARGET libdeflate)
pkg_check_modules(SDL3 REQUIRED IMPORTED_TARGET sdl3)
pkg_check_modules(X11 REQUIRED IMPORTED_TARGET x11)
find_package(miniz QUIET)
if(NOT miniz_FOUND)
    # Distributions without a miniz package (Debian, Ubuntu: #136): built from its release.
    include(FetchContent)
    FetchContent_Declare(miniz GIT_REPOSITORY https://github.com/richgel999/miniz.git GIT_TAG 3.1.2)
    FetchContent_MakeAvailable(miniz)
    if(NOT TARGET miniz::miniz)
        add_library(miniz::miniz ALIAS miniz)
    endif()
endif()
find_path(XBYAK_INCLUDE_DIR xbyak/xbyak.h)
if (NOT XBYAK_INCLUDE_DIR)
    include(FetchContent)
    FetchContent_Declare(bb_xbyak GIT_REPOSITORY https://github.com/herumi/xbyak.git GIT_TAG df112862583c2d327c1d9782fd28f53aa675f11d)
    FetchContent_MakeAvailable(bb_xbyak)
    set(XBYAK_INCLUDE_DIR "${bb_xbyak_SOURCE_DIR}")
endif()
include_directories(SYSTEM "${XBYAK_INCLUDE_DIR}")
find_package(Zydis REQUIRED)
# Some distro Zydis exports use a bare -lZycore. Resolve it at configure time so
# standalone cmake --build also works with Cachyborne's private dependency prefix.
get_target_property(BB_ZYDIS_LINKS Zydis::Zydis INTERFACE_LINK_LIBRARIES)
if ("Zycore" IN_LIST BB_ZYDIS_LINKS)
    find_library(BB_ZYCORE_LIBRARY NAMES Zycore REQUIRED)
    list(TRANSFORM BB_ZYDIS_LINKS REPLACE "^Zycore$" "${BB_ZYCORE_LIBRARY}")
    set_property(TARGET Zydis::Zydis PROPERTY INTERFACE_LINK_LIBRARIES "${BB_ZYDIS_LINKS}")
endif()
find_program(GLSLANG_COMPILER NAMES glslang glslangValidator REQUIRED)

set(SIRIT_USE_SYSTEM_SPIRV_HEADERS OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/sirit EXCLUDE_FROM_ALL)

# bbport: FSR 3.1 temporal upscaling / frame generation, native Vulkan (MIT, FireBurn/FSR-Vulkan).
set(FFX_VK_PORTABLE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FFX_VK_PORTABLE_BUILD_PROBE OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/fsr-vulkan EXCLUDE_FROM_ALL)

# Host shaders -> headers: SPIR-V arrays from glslang, in bbgpu/shaders/.
set(SHADER_INCLUDE ${CMAKE_CURRENT_BINARY_DIR}/include)
include(cmake/ShaderFingerprint.cmake)
file(GLOB_RECURSE BB_SHADER_FINGERPRINT_SOURCES CONFIGURE_DEPENDS
    shader/*.cpp shader/*.h gcn/*.cpp gcn/*.h
    render/pipeline*.cpp render/pipeline*.h render/permutation_indices.h
    render/shader_*.cpp render/shader_*.h
    common/*.h
    third_party/sirit/src/*.cpp third_party/sirit/src/*.h third_party/sirit/include/*.h
    third_party/sirit/externals/SPIRV-Headers/include/*.h
    third_party/sirit/externals/SPIRV-Headers/include/*.hpp
    cmake/ShaderFingerprint.cmake cmake/ShaderFingerprintDriver.cmake.in)
set(SHADER_HEADERS "")
function(add_shader_header SOURCE OUT_DIR SHADER_NAME)
    set(OUT ${OUT_DIR}/${SHADER_NAME}.h)
    string(TOUPPER ${SHADER_NAME} SYMBOL)
    set(DEFINES ${ARGN})
    list(TRANSFORM DEFINES PREPEND "-D")
    add_custom_command(OUTPUT ${OUT}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${OUT_DIR}
        COMMAND ${GLSLANG_COMPILER} --target-env vulkan1.3 -o ${OUT} --vn ${SYMBOL} ${DEFINES}
                -I${CMAKE_CURRENT_SOURCE_DIR}/third_party/amd-fsr1 ${SOURCE}
        COMMAND ${CMAKE_COMMAND} "-DHEADER_FILE=${OUT}" -P ${CMAKE_CURRENT_SOURCE_DIR}/render/shaders/header.cmake
        DEPENDS ${SOURCE} VERBATIM)
    set(SHADER_HEADERS ${SHADER_HEADERS} ${OUT} PARENT_SCOPE)
endfunction()
set(OWN_SHADER_DIR ${SHADER_INCLUDE}/bbgpu/shaders)
function(add_own_shader FILENAME SHADER_NAME)
    add_shader_header(${CMAKE_CURRENT_SOURCE_DIR}/${FILENAME} ${OWN_SHADER_DIR} ${SHADER_NAME} ${ARGN})
    set(SHADER_HEADERS ${SHADER_HEADERS} PARENT_SCOPE)
endfunction()
add_own_shader(render/shaders/depth_from_color.frag depth_from_color_frag)
add_own_shader(render/shaders/texel_copy.frag texel_copy_frag)
add_own_shader(render/shaders/texel_copy.frag texel_copy_msaa_frag MSAA_SOURCE=1)
add_own_shader(buffers/shaders/fault_scan.comp fault_scan_comp)
add_own_shader(render/shaders/fullscreen.vert fullscreen_vert)
add_own_shader(render/shaders/present_encode.frag present_encode_frag)
add_own_shader(upscale/shaders/camera_motion.comp camera_motion_comp)
# GT-VBAO in place of the game's SSAO (upscale/vk_vbao.cpp, docs/vbao.md; contributed by redditor69).
add_own_shader(upscale/shaders/vbao.comp vbao_comp)
add_own_shader(upscale/shaders/vbao_blur.comp vbao_blur_comp)
add_own_shader(upscale/shaders/vbao_blur.comp vbao_blur_rgba_comp OUTPUT_RGBA=1)
add_own_shader(upscale/shaders/vbao_view.comp vbao_view_comp)
add_own_shader(upscale/shaders/upscale_merge.comp upscale_merge_comp)
add_own_shader(upscale/shaders/upscale_reactive.comp upscale_reactive_comp)
add_own_shader(upscale/shaders/taa.comp taa_comp)
add_own_shader(upscale/shaders/taa_sharpen.comp taa_sharpen_comp)
add_own_shader(upscale/shaders/fsr1_easu.comp fsr1_easu_comp)
add_own_shader(upscale/shaders/taa_sharpen.comp taa_sharpen_ldr_comp LDR_OUTPUT=1)
add_own_shader(upscale/shaders/fsr4_color.comp fsr4_decode_comp)
add_own_shader(upscale/shaders/fsr4_color.comp fsr4_encode_comp ENCODE=1)
add_own_shader(upscale/shaders/fsr4_reactive.comp fsr4_reactive_comp)
add_own_shader(upscale/shaders/fg_display_lut.comp fg_display_lut_comp)
# SMAA 1x for anti_aliasing=smaa (upscale/vk_smaa.cpp; third_party/smaa, MIT).
add_own_shader(upscale/shaders/smaa.comp smaa_edges_comp PASS=0)
add_own_shader(upscale/shaders/smaa.comp smaa_weights_comp PASS=1)
add_own_shader(upscale/shaders/smaa.comp smaa_blend_comp PASS=2)
# bbport: FSR 3.1 frame generation passes compiled here (ffx_vk.cpp ffxVkShaderOverride): the SDK's
# GLSL with its own headers, as FidelityFX_SC builds them (-Os, Vulkan 1.2), changed where faster.
set(FFX_GPU_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/fsr-vulkan/upstream/ffx-1.1.4/sdk/include/FidelityFX/gpu)
# The port's changed copies of SDK headers: a change to one rebuilds every pass.
file(GLOB_RECURSE FFX_OWN_HEADERS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/upscale/shaders/ffx/*.h)
function(add_ffx_shader FILENAME SHADER_NAME)
    set(OUT ${OWN_SHADER_DIR}/${SHADER_NAME}.h)
    string(TOUPPER ${SHADER_NAME} SYMBOL)
    set(DEFINES ${ARGN})
    list(TRANSFORM DEFINES PREPEND "-D")
    add_custom_command(OUTPUT ${OUT}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${OWN_SHADER_DIR}
        COMMAND ${GLSLANG_COMPILER} --target-env vulkan1.2 -S comp -Os -o ${OUT} --vn ${SYMBOL}
                -DFFX_GPU=1 -DFFX_GLSL=1 ${DEFINES} -I${CMAKE_CURRENT_SOURCE_DIR}/upscale/shaders/ffx -I${FFX_GPU_DIR}
                ${CMAKE_CURRENT_SOURCE_DIR}/${FILENAME}
        COMMAND ${CMAKE_COMMAND} "-DHEADER_FILE=${OUT}" -P ${CMAKE_CURRENT_SOURCE_DIR}/render/shaders/header.cmake
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${FILENAME} ${FFX_OWN_HEADERS} VERBATIM)
    set(SHADER_HEADERS ${SHADER_HEADERS} ${OUT} PARENT_SCOPE)
endfunction()
add_ffx_shader(upscale/shaders/ffx/of_v5.comp ffx_of_v5 FFX_HALF=0 FFX_OPTICALFLOW_OPTION_HDR_COLOR_INPUT=0)
add_ffx_shader(upscale/shaders/ffx/of_v5.comp ffx_of_v5_hdr FFX_HALF=0 FFX_OPTICALFLOW_OPTION_HDR_COLOR_INPUT=1)
add_ffx_shader(upscale/shaders/ffx/of_v5.comp ffx_of_v5_16 FFX_HALF=1 FFX_OPTICALFLOW_OPTION_HDR_COLOR_INPUT=0)
add_ffx_shader(upscale/shaders/ffx/of_v5.comp ffx_of_v5_16_hdr FFX_HALF=1 FFX_OPTICALFLOW_OPTION_HDR_COLOR_INPUT=1)
# BB_OF_FAST: the finest optical flow level takes the coarser one's vectors (no search).
add_ffx_shader(upscale/shaders/ffx/of_v5.comp ffx_of_v5_fast FFX_HALF=0 FFX_OPTICALFLOW_OPTION_HDR_COLOR_INPUT=0 BB_OF_LEVEL0_PREDICT=1)
add_ffx_shader(upscale/shaders/ffx/of_v5.comp ffx_of_v5_hdr_fast FFX_HALF=0 FFX_OPTICALFLOW_OPTION_HDR_COLOR_INPUT=1 BB_OF_LEVEL0_PREDICT=1)
add_ffx_shader(upscale/shaders/ffx/of_v5.comp ffx_of_v5_16_fast FFX_HALF=1 FFX_OPTICALFLOW_OPTION_HDR_COLOR_INPUT=0 BB_OF_LEVEL0_PREDICT=1)
add_ffx_shader(upscale/shaders/ffx/of_v5.comp ffx_of_v5_16_hdr_fast FFX_HALF=1 FFX_OPTICALFLOW_OPTION_HDR_COLOR_INPUT=1 BB_OF_LEVEL0_PREDICT=1)
add_ffx_shader(upscale/shaders/ffx/fi_interpolation.comp ffx_fi_interpolation FFX_HALF=0)
add_ffx_shader(upscale/shaders/ffx/fi_interpolation.comp ffx_fi_interpolation_check FFX_HALF=0 BB_FI_HARDWARE_BILINEAR=2)
add_own_shader(render/shaders/buffer_multi_copy.comp buffer_multi_copy_comp)
add_own_shader(render/shaders/contact_shadow.comp contact_shadow_comp)
add_own_shader(render/shaders/atlas_decode.comp atlas_decode_comp)
add_own_shader(render/shaders/occlusion_resolve.comp occlusion_resolve_comp)
add_own_shader(render/shaders/gpu_timestamp.comp gpu_timestamp_comp)
add_own_shader(render/shaders/indirect_guard.comp indirect_guard_comp)
add_own_shader(render/shaders/indirect_draw_guard.comp indirect_draw_guard_comp)
add_own_shader(upscale/shaders/depth_resample.frag depth_resample_frag)
add_own_shader(upscale/shaders/depth_resample.frag depth_stencil_resample_frag HAS_STENCIL=1)
add_own_shader(upscale/shaders/depth_resample.frag depth_stencil_bits_frag STENCIL_BITS=1)
add_own_shader(upscale/shaders/camera_motion.comp camera_motion_debug_comp DEBUG_OVERLAY=1)
foreach(BPP IN ITEMS 8 16 32 64 96 128)
    add_own_shader(textures/shaders/detile.comp detile_micro_${BPP}_comp BITS_PER_PIXEL=${BPP} IS_MACRO_TILED=0)
    add_own_shader(textures/shaders/detile.comp detile_macro_${BPP}_comp BITS_PER_PIXEL=${BPP} IS_MACRO_TILED=1)
endforeach()
add_custom_target(host_shaders DEPENDS ${SHADER_HEADERS})

file(GLOB_RECURSE RECOMPILER CONFIGURE_DEPENDS shader/*.cpp)
# Own utilities (types, logging, paths, locks, containers): common/.
file(GLOB COMMON CONFIGURE_DEPENDS common/*.cpp common/logging/*.cpp)
# SHA-256 serves both the offline script tools and native plugin ELF identity.
# Own system libraries (Gnm, VideoOut, AvPlayer, event queues, interrupts): platform/.
file(GLOB_RECURSE PLATFORM_SOURCES CONFIGURE_DEPENDS platform/*.cpp)
# platform/win32: the POSIX subset over Win32 (with its headers in platform/win32/include).
    list(FILTER PLATFORM_SOURCES EXCLUDE REGEX "/platform/win32/")
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil libswscale libswresample)
file(GLOB SHIM_SOURCES CONFIGURE_DEPENDS shim/*.cpp shim/*/*.cpp)
# The layer's own modules (independent of the shadPS4 code): layer/.
file(GLOB LAYER_SOURCES CONFIGURE_DEPENDS layer/*.cpp)
# Game profiles (what the translator knows about one game, out of its core): games/.
file(GLOB GAME_SOURCES CONFIGURE_DEPENDS games/*.cpp games/*/*.cpp)
# These helpers currently serve the explicit offline targets below.
list(REMOVE_ITEM GAME_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/games/bloodborne/esd.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/games/bloodborne/rebirth_script.cpp")
# The translator's own command processor (docs/ROADMAP.md, step 2): cp/.
file(GLOB CP_SOURCES CONFIGURE_DEPENDS cp/*.cpp memory/*.cpp gcn/*.cpp textures/*.cpp buffers/*.cpp vulkan/*.cpp)
# The translator's rendering pieces (labels, timestamps, occlusion, draw preparation, profiling,
# frame capture, prompts): render/. Temporal upscaling, motion vectors, scene resolution: upscale/.
file(GLOB RENDER_SOURCES CONFIGURE_DEPENDS render/*.cpp)
file(GLOB UPSCALE_SOURCES CONFIGURE_DEPENDS upscale/*.cpp upscale/fsr411/*.cpp)
    set(BBGPU_LIBRARY_TYPE SHARED)
add_library(bbgpu ${BBGPU_LIBRARY_TYPE} ${RECOMPILER} ${COMMON} ${PLATFORM_SOURCES} ${SHIM_SOURCES} ${LAYER_SOURCES} ${GAME_SOURCES} ${CP_SOURCES}
    ${RENDER_SOURCES} ${UPSCALE_SOURCES})
add_dependencies(bbgpu host_shaders)
# In-game settings menu: Dear ImGui (third_party/imgui, MIT) with its Vulkan backend, and an
# embedded Cyrillic font (third_party/fonts).
target_sources(bbgpu PRIVATE third_party/imgui/imgui.cpp third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp third_party/imgui/imgui_widgets.cpp
    third_party/imgui/backends/imgui_impl_vulkan.cpp)
target_include_directories(bbgpu PRIVATE third_party/imgui third_party/imgui/backends)
# Dear ImGui stays private to libbbgpu.so. The library is linked into bb-probe, so its exports are
# in the global symbol scope; a Vulkan layer carrying its own Dear ImGui without -Bsymbolic
# (vkShade 0.1) had its ImGui_ImplVulkan_Init bound to this copy, which called the Vulkan loader
# with the layer's down-chain physical device handle ("Invalid physicalDevice", abort).
set_source_files_properties(third_party/imgui/imgui.cpp third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp third_party/imgui/imgui_widgets.cpp
    third_party/imgui/backends/imgui_impl_vulkan.cpp PROPERTIES
    COMPILE_OPTIONS "-fvisibility=hidden;-fvisibility-inlines-hidden")
target_compile_definitions(bbgpu PRIVATE IMGUI_IMPL_VULKAN_NO_PROTOTYPES
    BB_FONT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts/DejaVuSans.ttf")
set_source_files_properties(shim/bbport_overlay.cpp PROPERTIES
    OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts/DejaVuSans.ttf")
target_include_directories(bbgpu PRIVATE . shim layer games cp "${SHADER_INCLUDE}/$<CONFIG>" ${SHADER_INCLUDE} third_party/gcn/include third_party/half/include)
target_compile_definitions(bbgpu PRIVATE BOOST_ASIO_STANDALONE NDEBUG)
target_compile_options(bbgpu PRIVATE -Wno-deprecated-declarations -Wno-missing-field-initializers)
# Loaded with bb-probe at startup (DT_NEEDED, never dlopen): thread_local access without a
# TLS descriptor call (_dl_tlsdesc_return was ~2% of the draw recording thread).
    target_compile_options(bbgpu PRIVATE -ftls-model=initial-exec)
    # Runtime symbols (runtime_memory_*, runtime_process_*) resolve against bb-probe.
    target_link_options(bbgpu PRIVATE -Wl,--allow-shlib-undefined)
target_link_libraries(bbgpu PRIVATE ffx-vulkan::portable ffx-vulkan::fsr4-v07-vulkan PkgConfig::LIBDEFLATE)
# Trophy names: trophy00.trp decrypted with the user's trophy key (shim/bbport_trophies.cpp).
# Without OpenSSL the unlocks are kept and shown by number.
find_package(OpenSSL QUIET COMPONENTS Crypto)
if (OpenSSL_FOUND)
    target_link_libraries(bbgpu PRIVATE OpenSSL::Crypto)
    target_compile_definitions(bbgpu PRIVATE BB_TROPHY_DECRYPT=1)
else()
    message(STATUS "Trophy names: off (OpenSSL's libcrypto missing; unlocks shown by number)")
endif()
target_link_libraries(bbgpu PUBLIC Vulkan::Headers fmt::fmt Boost::headers magic_enum::magic_enum
    tsl::robin_map GPUOpen::VulkanMemoryAllocator PkgConfig::XXHASH PkgConfig::SDL3 PkgConfig::FFMPEG miniz::miniz Zydis::Zydis sirit)
    target_link_libraries(bbgpu PUBLIC PkgConfig::X11)

# Explicit test targets; neither launches the game nor needs a Vulkan device.
add_executable(trophy-test EXCLUDE_FROM_ALL ../tests/test_trophies.cpp shim/bbport_trophy_data.cpp)
target_compile_options(trophy-test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
target_include_directories(trophy-test PRIVATE shim)
add_executable(motion-history-test EXCLUDE_FROM_ALL ../tests/test_motion_history.cpp)
target_compile_options(motion-history-test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
target_include_directories(motion-history-test PRIVATE ..)
add_executable(record-chunk-test EXCLUDE_FROM_ALL ../tests/test_record_chunk.cpp)
target_compile_options(record-chunk-test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
target_include_directories(record-chunk-test PRIVATE . shim)
target_link_libraries(record-chunk-test PRIVATE Vulkan::Headers)
add_executable(motion-shader-test EXCLUDE_FROM_ALL ../tests/test_motion_shaders.cpp ../tests/test_gpu_runtime_stubs.cpp)
target_include_directories(motion-shader-test PRIVATE . shim ${SHADER_INCLUDE} third_party/gcn/include)
target_link_libraries(motion-shader-test PRIVATE bbgpu)
add_executable(ui-composition-test EXCLUDE_FROM_ALL ../tests/test_ui_composition.cpp)
target_compile_options(ui-composition-test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
target_include_directories(ui-composition-test PRIVATE ..)
add_executable(cp-decoder-test EXCLUDE_FROM_ALL ../tests/test_cp_decoder.cpp cp/cp_decoder.cpp)
target_compile_options(cp-decoder-test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
target_include_directories(cp-decoder-test PRIVATE cp)
add_executable(fx-init-clear-test EXCLUDE_FROM_ALL ../tests/test_fx_init_clear.cpp cp/cp_decoder.cpp)
target_compile_options(fx-init-clear-test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
target_include_directories(fx-init-clear-test PRIVATE . shim)
add_executable(effect-flags-test EXCLUDE_FROM_ALL ../tests/test_effect_flags.cpp)
target_compile_options(effect-flags-test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
target_include_directories(effect-flags-test PRIVATE shim)
add_executable(settings-test EXCLUDE_FROM_ALL ../tests/test_settings.cpp shim/bbport_settings.cpp)
target_compile_options(settings-test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
target_include_directories(settings-test PRIVATE . .. shim)

# The clip-space matrices DLSS Frame Generation gets (upscale/camera_matrices.h): no GPU.
add_executable(camera-matrices-test EXCLUDE_FROM_ALL ../tests/test_camera_matrices.cpp)
target_compile_options(camera-matrices-test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
target_include_directories(camera-matrices-test PRIVATE ..)

add_executable(scene-resolution-test EXCLUDE_FROM_ALL ../tests/test_scene_resolution.cpp ../tests/test_gpu_runtime_stubs.cpp)
target_include_directories(scene-resolution-test PRIVATE . shim ${SHADER_INCLUDE} third_party/gcn/include)
# The test's Vulkan-Hpp inline methods have a different set of platform extensions
# from vk_platform.cpp. Keep them local and use an explicit dispatcher in the test.
target_compile_options(scene-resolution-test PRIVATE -UNDEBUG -fPIC)
set_source_files_properties(../tests/test_scene_resolution.cpp PROPERTIES
    COMPILE_OPTIONS "-fvisibility=hidden;-fvisibility-inlines-hidden")
target_link_libraries(scene-resolution-test PRIVATE bbgpu)
add_executable(taa-shader-test EXCLUDE_FROM_ALL ../tests/test_taa_shader.cpp ../tests/test_gpu_runtime_stubs.cpp)
target_include_directories(taa-shader-test PRIVATE . shim ${SHADER_INCLUDE} third_party/gcn/include)
target_compile_options(taa-shader-test PRIVATE -UNDEBUG -fPIC)
set_source_files_properties(../tests/test_taa_shader.cpp PROPERTIES
    COMPILE_OPTIONS "-fvisibility=hidden;-fvisibility-inlines-hidden")
target_link_libraries(taa-shader-test PRIVATE bbgpu)
add_dependencies(taa-shader-test host_shaders)

add_executable(camera-motion-test EXCLUDE_FROM_ALL ../tests/test_camera_motion.cpp ../tests/test_gpu_runtime_stubs.cpp)
target_include_directories(camera-motion-test PRIVATE . shim ${SHADER_INCLUDE} third_party/gcn/include)
target_compile_options(camera-motion-test PRIVATE -UNDEBUG -fPIC)
target_link_libraries(camera-motion-test PRIVATE bbgpu)
add_dependencies(camera-motion-test host_shaders)

add_executable(translation-memory-test EXCLUDE_FROM_ALL ../tests/test_translation_memory.cpp
    ../tests/test_gpu_runtime_stubs.cpp)
target_include_directories(translation-memory-test PRIVATE . shim ${SHADER_INCLUDE} third_party/gcn/include)
target_compile_options(translation-memory-test PRIVATE -UNDEBUG -fPIC)
set_source_files_properties(../tests/test_translation_memory.cpp PROPERTIES
    COMPILE_OPTIONS "-fvisibility=hidden;-fvisibility-inlines-hidden")
target_link_libraries(translation-memory-test PRIVATE bbgpu)

# Spare images on a real GPU (textures/image_spares.h): the time a texture costs the recording
# thread with and without them, the GPU busy.
add_executable(image-spares-test EXCLUDE_FROM_ALL ../tests/test_image_spares.cpp
    textures/image_spares.cpp vulkan/vk_common.cpp)
target_include_directories(image-spares-test PRIVATE . shim)
target_compile_options(image-spares-test PRIVATE -UNDEBUG)
target_link_libraries(image-spares-test PRIVATE Vulkan::Headers GPUOpen::VulkanMemoryAllocator)

# bbport: FSR 4 benchmark outside the game (tools/fsr4_bench.cpp): ninja -C out/gpu fsr4-bench.
add_executable(upscaler-support-test EXCLUDE_FROM_ALL ../tests/test_upscaler_support.cpp
    shim/bbport_settings.cpp)
target_include_directories(upscaler-support-test PRIVATE . shim)
target_compile_options(upscaler-support-test PRIVATE -UNDEBUG)

add_executable(fsr4-bench EXCLUDE_FROM_ALL tools/fsr4_bench.cpp
    upscale/fsr411/fsr411.cpp)
target_include_directories(fsr4-bench PRIVATE . upscale/fsr411)
target_link_libraries(fsr4-bench PRIVATE ffx-vulkan::fsr4-v07-vulkan ffx-vulkan::fsr4-v07-assets Vulkan::Vulkan)

# bbport online module (gpu/bbnet20, libbbnet20.so): built when its libraries are there (shell.nix);
# BB_NET=OFF leaves it out. Without it the port plays offline.
option(BB_NET "Build the online module (shadNet)" ON)
if (BB_NET)
    find_package(Protobuf CONFIG QUIET)
    find_package(nlohmann_json CONFIG QUIET)
    find_package(OpenSSL QUIET)
    find_package(ZLIB QUIET)
    find_path(MINIUPNPC_INCLUDE miniupnpc.h PATH_SUFFIXES miniupnpc)
    find_library(MINIUPNPC_LIBRARY NAMES miniupnpc)
    if (Protobuf_FOUND AND nlohmann_json_FOUND AND OpenSSL_FOUND AND ZLIB_FOUND AND
        MINIUPNPC_INCLUDE AND MINIUPNPC_LIBRARY)
        add_subdirectory(bbnet20)
        message(STATUS "Online module: on (libbbnet20.so)")
    else()
        # Named one by one (#145: "the build has no online module" with no hint why).
        set(missing "")
        foreach(dep Protobuf nlohmann_json OpenSSL ZLIB)
            if (NOT ${dep}_FOUND)
                list(APPEND missing ${dep})
            endif()
        endforeach()
        if (NOT MINIUPNPC_INCLUDE OR NOT MINIUPNPC_LIBRARY)
            list(APPEND missing miniupnpc)
        endif()
        list(JOIN missing ", " missing)
        message(STATUS "Online module: off (missing: ${missing}; shell.nix lists the packages)")
    endif()
endif()

# bbport: the shader corpus replay (render/shader_capture.h): ninja -C out/gpu shader-corpus-test.
add_executable(shader-corpus-test EXCLUDE_FROM_ALL ../tests/test_shader_corpus.cpp
    ../tests/test_gpu_runtime_stubs.cpp)
target_include_directories(shader-corpus-test PRIVATE . shim ${SHADER_INCLUDE} third_party/gcn/include third_party/half/include)
target_compile_options(shader-corpus-test PRIVATE -UNDEBUG -fPIC)
target_link_libraries(shader-corpus-test PRIVATE bbgpu)

add_executable(srt-walker-fault-test EXCLUDE_FROM_ALL ../tests/test_srt_walker_fault.cpp
    ../tests/test_gpu_runtime_stubs.cpp)
target_include_directories(srt-walker-fault-test PRIVATE . shim ${SHADER_INCLUDE} third_party/gcn/include third_party/half/include)
target_compile_options(srt-walker-fault-test PRIVATE -UNDEBUG -Wall -Wextra)
target_link_libraries(srt-walker-fault-test PRIVATE bbgpu)

add_executable(spanish-game-menu-test EXCLUDE_FROM_ALL ../tests/test_spanish_game_menu.cpp
    ../tests/test_gpu_runtime_stubs.cpp)
target_include_directories(spanish-game-menu-test PRIVATE . shim)
target_compile_options(spanish-game-menu-test PRIVATE -UNDEBUG -Wall -Wextra)
target_link_libraries(spanish-game-menu-test PRIVATE bbgpu)

add_executable(shader-cache-invariants-test EXCLUDE_FROM_ALL ../tests/test_shader_cache_invariants.cpp
    ../tests/test_gpu_runtime_stubs.cpp)
target_include_directories(shader-cache-invariants-test PRIVATE . shim ${SHADER_INCLUDE} third_party/gcn/include third_party/half/include)
target_compile_options(shader-cache-invariants-test PRIVATE -UNDEBUG -Wall -Wextra)
target_link_libraries(shader-cache-invariants-test PRIVATE bbgpu)

# All project-owned regression targets are explicit, including assertions in Release builds.
enable_testing()
add_executable(guest-image-hash-test EXCLUDE_FROM_ALL ../tests/test_guest_image_hash.cpp textures/guest_image_hash.cpp)
target_include_directories(guest-image-hash-test PRIVATE .)
target_compile_options(guest-image-hash-test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
target_link_libraries(guest-image-hash-test PRIVATE PkgConfig::XXHASH)

add_executable(image-write-baseline-test EXCLUDE_FROM_ALL ../tests/test_image_write_baseline.cpp ../tests/test_gpu_runtime_stubs.cpp)
target_include_directories(image-write-baseline-test PRIVATE . shim ${SHADER_INCLUDE} third_party/gcn/include)
target_compile_options(image-write-baseline-test PRIVATE -UNDEBUG -fPIC)
target_link_libraries(image-write-baseline-test PRIVATE bbgpu PkgConfig::XXHASH)

add_executable(image-download-test EXCLUDE_FROM_ALL ../tests/test_image_download.cpp ../tests/test_gpu_runtime_stubs.cpp)
target_include_directories(image-download-test PRIVATE . shim ${SHADER_INCLUDE} third_party/gcn/include)
target_compile_options(image-download-test PRIVATE -UNDEBUG -fPIC)
target_link_libraries(image-download-test PRIVATE bbgpu)

add_executable(draw-pipe-steal-test EXCLUDE_FROM_ALL ../tests/test_draw_pipe_steal.cpp ../tests/test_gpu_runtime_stubs.cpp)
target_include_directories(draw-pipe-steal-test PRIVATE . shim)
target_compile_options(draw-pipe-steal-test PRIVATE -UNDEBUG -Wall -Wextra)
target_link_libraries(draw-pipe-steal-test PRIVATE bbgpu)

add_executable(frame-clock-hooks-test EXCLUDE_FROM_ALL ../tests/test_frame_clock_hooks.cpp ../tests/test_gpu_runtime_stubs.cpp)
target_include_directories(frame-clock-hooks-test PRIVATE . shim ../tests)
target_compile_options(frame-clock-hooks-test PRIVATE -UNDEBUG -fPIE)
target_link_options(frame-clock-hooks-test PRIVATE -pie)
target_link_libraries(frame-clock-hooks-test PRIVATE bbgpu)

add_executable(ragdoll-clock-test EXCLUDE_FROM_ALL ../tests/test_ragdoll_clock.cpp)
target_include_directories(ragdoll-clock-test PRIVATE . shim ../tests)
target_compile_options(ragdoll-clock-test PRIVATE -UNDEBUG -fPIE)
target_link_options(ragdoll-clock-test PRIVATE -pie)

add_executable(frame-clock-test EXCLUDE_FROM_ALL ../tests/test_frame_clock.cpp)
target_include_directories(frame-clock-test PRIVATE shim)
target_compile_options(frame-clock-test PRIVATE -UNDEBUG)
foreach(timing_kind IN ITEMS sync replay_manipulator)
    add_executable(chr-${timing_kind}-test EXCLUDE_FROM_ALL ../tests/test_chr_${timing_kind}.cpp)
    target_include_directories(chr-${timing_kind}-test PRIVATE . shim ../tests)
    target_compile_options(chr-${timing_kind}-test PRIVATE -UNDEBUG -fPIE)
    target_link_options(chr-${timing_kind}-test PRIVATE -pie)
endforeach()

add_executable(guest-hook-replacement-test EXCLUDE_FROM_ALL ../tests/test_guest_hook_replacement.cpp ../tests/test_gpu_runtime_stubs.cpp)
target_include_directories(guest-hook-replacement-test PRIVATE . shim)
target_compile_options(guest-hook-replacement-test PRIVATE -UNDEBUG)
target_link_libraries(guest-hook-replacement-test PRIVATE bbgpu)

add_executable(gx-job-barrier-test EXCLUDE_FROM_ALL ../tests/test_gx_job_barrier.cpp)
target_include_directories(gx-job-barrier-test PRIVATE .)
target_compile_options(gx-job-barrier-test PRIVATE -UNDEBUG)

add_executable(menu-pointer-test EXCLUDE_FROM_ALL ../tests/test_menu_pointer.cpp)
target_include_directories(menu-pointer-test PRIVATE games)
target_compile_options(menu-pointer-test PRIVATE -UNDEBUG)

add_executable(effect-time-test EXCLUDE_FROM_ALL ../tests/test_effect_time.cpp)
target_include_directories(effect-time-test PRIVATE shim)
target_compile_options(effect-time-test PRIVATE -UNDEBUG)

add_executable(guest-guards-test EXCLUDE_FROM_ALL ../tests/test_guest_guards.cpp)
target_include_directories(guest-guards-test PRIVATE shim)
target_compile_options(guest-guards-test PRIVATE -UNDEBUG)

add_executable(hybrid-affinity-test EXCLUDE_FROM_ALL ../tests/test_hybrid_affinity.cpp shim/bbport_affinity.cpp)
target_include_directories(hybrid-affinity-test PRIVATE .)
target_compile_options(hybrid-affinity-test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
target_link_libraries(hybrid-affinity-test PRIVATE Threads::Threads)

set(CACHYBORNE_TEST_TARGETS draw-pipe-steal-test frame-clock-hooks-test ragdoll-clock-test frame-clock-test chr-sync-test chr-replay_manipulator-test guest-hook-replacement-test gx-job-barrier-test menu-pointer-test guest-guards-test effect-time-test image-download-test image-write-baseline-test guest-image-hash-test shader-cache-invariants-test trophy-test camera-matrices-test image-spares-test spanish-game-menu-test motion-history-test motion-shader-test ui-composition-test
    cp-decoder-test fx-init-clear-test effect-flags-test settings-test scene-resolution-test
    taa-shader-test camera-motion-test translation-memory-test upscaler-support-test
    shader-corpus-test srt-walker-fault-test)
add_executable(staging-pool-test EXCLUDE_FROM_ALL ../tests/test_staging_pool.cpp ../tests/test_gpu_runtime_stubs.cpp)
target_include_directories(staging-pool-test PRIVATE . shim ${SHADER_INCLUDE} third_party/gcn/include)
target_compile_options(staging-pool-test PRIVATE -UNDEBUG -fPIC)
target_link_libraries(staging-pool-test PRIVATE bbgpu)
add_executable(record-data-test EXCLUDE_FROM_ALL ../tests/test_record_data.cpp ../tests/test_gpu_runtime_stubs.cpp)
target_include_directories(record-data-test PRIVATE . shim)
target_compile_options(record-data-test PRIVATE -UNDEBUG -fPIC -Wall -Wextra -Werror)
target_link_libraries(record-data-test PRIVATE bbgpu)

add_executable(timeline-wait-test EXCLUDE_FROM_ALL ../tests/test_timeline_wait.cpp ../tests/test_gpu_runtime_stubs.cpp)
target_include_directories(timeline-wait-test PRIVATE . shim)
target_compile_options(timeline-wait-test PRIVATE -UNDEBUG -fPIC -Wall -Wextra -Werror)
target_link_libraries(timeline-wait-test PRIVATE bbgpu)
list(APPEND CACHYBORNE_TEST_TARGETS timeline-wait-test)

add_executable(progress-request-test EXCLUDE_FROM_ALL ../tests/test_progress_request.cpp)
target_include_directories(progress-request-test PRIVATE .)
target_compile_options(progress-request-test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
target_link_libraries(progress-request-test PRIVATE Threads::Threads)
list(APPEND CACHYBORNE_TEST_TARGETS progress-request-test)

add_executable(cp-progress-test EXCLUDE_FROM_ALL ../tests/test_cp_progress.cpp ../tests/test_gpu_runtime_stubs.cpp)
target_include_directories(cp-progress-test PRIVATE . shim)
target_compile_options(cp-progress-test PRIVATE -UNDEBUG -fPIC -Wall -Wextra -Werror)
target_link_libraries(cp-progress-test PRIVATE bbgpu)
list(APPEND CACHYBORNE_TEST_TARGETS cp-progress-test)

add_executable(frame-capture-test EXCLUDE_FROM_ALL ../tests/test_frame_capture.cpp render/vk_frame_capture.cpp)
target_include_directories(frame-capture-test PRIVATE .)
target_compile_options(frame-capture-test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
target_link_libraries(frame-capture-test PRIVATE Vulkan::Headers fmt::fmt)
list(APPEND CACHYBORNE_TEST_TARGETS frame-capture-test)

# Explicit integration harness only; never preloaded by a normal launch.
add_library(gpu-progress-preload MODULE EXCLUDE_FROM_ALL ../tests/gpu_progress_preload.cpp)
target_include_directories(gpu-progress-preload PRIVATE . shim)
target_compile_options(gpu-progress-preload PRIVATE -Wall -Wextra -Werror)
target_link_libraries(gpu-progress-preload PRIVATE bbgpu ${CMAKE_DL_LIBS})

add_executable(copy-filter-test EXCLUDE_FROM_ALL ../tests/test_copy_filter.cpp ../tests/test_gpu_runtime_stubs.cpp)
target_include_directories(copy-filter-test PRIVATE . shim)
target_compile_options(copy-filter-test PRIVATE -UNDEBUG -fPIC -Wall -Wextra -Werror)
target_link_libraries(copy-filter-test PRIVATE bbgpu)

add_executable(write-ticks-lifetime-test EXCLUDE_FROM_ALL ../tests/test_write_ticks_lifetime.cpp ../tests/test_gpu_runtime_stubs.cpp)
target_include_directories(write-ticks-lifetime-test PRIVATE . shim)
target_compile_options(write-ticks-lifetime-test PRIVATE -UNDEBUG -fPIC -Wall -Wextra -Werror)
target_link_libraries(write-ticks-lifetime-test PRIVATE bbgpu)
list(APPEND CACHYBORNE_TEST_TARGETS write-ticks-lifetime-test)

add_executable(interrupt-lifetime-test EXCLUDE_FROM_ALL ../tests/test_interrupt_lifetime.cpp ../tests/test_gpu_runtime_stubs.cpp)
target_include_directories(interrupt-lifetime-test PRIVATE . shim)
target_compile_options(interrupt-lifetime-test PRIVATE -UNDEBUG -fPIC -Wall -Wextra -Werror)
target_link_libraries(interrupt-lifetime-test PRIVATE bbgpu)
list(APPEND CACHYBORNE_TEST_TARGETS interrupt-lifetime-test)

add_executable(params-test EXCLUDE_FROM_ALL ../tests/test_params.cpp games/bloodborne/params.cpp)
target_include_directories(params-test PRIVATE .)
target_compile_options(params-test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
list(APPEND CACHYBORNE_TEST_TARGETS params-test)

add_executable(blob-store-test EXCLUDE_FROM_ALL ../tests/test_blob_store.cpp
    ../tests/test_gpu_runtime_stubs.cpp)
target_include_directories(blob-store-test PRIVATE . shim)
target_compile_options(blob-store-test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
target_link_libraries(blob-store-test PRIVATE bbgpu)
list(APPEND CACHYBORNE_TEST_TARGETS blob-store-test)

# Offline talk-script preparation. Kept out of the runtime until the complete
# altar handshake, localized messages and undo path have been validated.
set(BB_TALK_SCRIPT_SOURCES games/bloodborne/esd.cpp games/bloodborne/rebirth_script.cpp common/sha256.cpp)
add_executable(esd-test EXCLUDE_FROM_ALL ../tests/test_esd.cpp ${BB_TALK_SCRIPT_SOURCES})
target_include_directories(esd-test PRIVATE .)
target_compile_options(esd-test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
list(APPEND CACHYBORNE_TEST_TARGETS esd-test)
add_executable(rebirth-script-tool EXCLUDE_FROM_ALL ../tools/rebirth_script.cpp ${BB_TALK_SCRIPT_SOURCES})
target_include_directories(rebirth-script-tool PRIVATE .)
target_compile_options(rebirth-script-tool PRIVATE -Wall -Wextra -Werror)
add_dependencies(esd-test rebirth-script-tool)

add_executable(rebirth-plan-test EXCLUDE_FROM_ALL ../tests/test_rebirth_plan.cpp)
target_include_directories(rebirth-plan-test PRIVATE .)
target_compile_options(rebirth-plan-test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
add_executable(rebirth-price-test EXCLUDE_FROM_ALL ../tests/test_rebirth_price.cpp shim/bbport_guest_call.cpp)
target_include_directories(rebirth-price-test PRIVATE . shim ../tests)
target_compile_options(rebirth-price-test PRIVATE -UNDEBUG -fPIE -Wall -Wextra -Werror)
target_link_options(rebirth-price-test PRIVATE -pie)
list(APPEND CACHYBORNE_TEST_TARGETS rebirth-plan-test rebirth-price-test)
add_executable(rebirth-session-test EXCLUDE_FROM_ALL ../tests/test_rebirth_session.cpp)
target_include_directories(rebirth-session-test PRIVATE .)
target_compile_options(rebirth-session-test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
list(APPEND CACHYBORNE_TEST_TARGETS rebirth-session-test)

foreach(water_test IN ITEMS water-gate-model sfx-schedule)
    string(REPLACE "-" "_" water_source "${water_test}")
    add_executable(${water_test}-test EXCLUDE_FROM_ALL ../tests/test_${water_source}.cpp)
    target_include_directories(${water_test}-test PRIVATE .)
    target_compile_options(${water_test}-test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
    list(APPEND CACHYBORNE_TEST_TARGETS ${water_test}-test)
endforeach()

add_executable(limiter-margin-test EXCLUDE_FROM_ALL ../tests/test_limiter_margin.cpp)
target_include_directories(limiter-margin-test PRIVATE shim)
target_compile_options(limiter-margin-test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
add_executable(limiter-hooks-test EXCLUDE_FROM_ALL ../tests/test_limiter_hooks.cpp ../tests/test_gpu_runtime_stubs.cpp)
target_include_directories(limiter-hooks-test PRIVATE . shim ../tests)
target_compile_options(limiter-hooks-test PRIVATE -UNDEBUG -fPIE -Wall -Wextra -Werror)
target_link_options(limiter-hooks-test PRIVATE -pie)
target_link_libraries(limiter-hooks-test PRIVATE bbgpu)
list(APPEND CACHYBORNE_TEST_TARGETS limiter-margin-test limiter-hooks-test)

add_executable(flip-pacing-test EXCLUDE_FROM_ALL ../tests/test_flip_pacing.cpp)
target_include_directories(flip-pacing-test PRIVATE .)
target_compile_options(flip-pacing-test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
list(APPEND CACHYBORNE_TEST_TARGETS flip-pacing-test)

add_executable(sound-wake-test EXCLUDE_FROM_ALL ../tests/test_sound_wake.cpp)
target_include_directories(sound-wake-test PRIVATE .)
target_compile_options(sound-wake-test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
target_link_libraries(sound-wake-test PRIVATE Threads::Threads)
add_executable(sound-wake-hooks-test EXCLUDE_FROM_ALL ../tests/test_sound_wake_hooks.cpp ../tests/test_gpu_runtime_stubs.cpp)
target_include_directories(sound-wake-hooks-test PRIVATE . shim)
target_compile_options(sound-wake-hooks-test PRIVATE -UNDEBUG -fPIC -Wall -Wextra -Werror)
target_link_libraries(sound-wake-hooks-test PRIVATE bbgpu)
list(APPEND CACHYBORNE_TEST_TARGETS sound-wake-test sound-wake-hooks-test)

foreach(feature_test IN ITEMS plugin-cleanup guest-patch plugin-image plugin-callbacks-frame guest-call bloodletter water-gate guest-hook-conditional frame-tasks menu-steps appearance rebirth fmod-project-init sf-text-layout gx-rename-retail gx-reclaim save-quit occlusion-fresh)
    string(REPLACE "-" "_" feature_source "${feature_test}")
    add_executable(${feature_test}-test EXCLUDE_FROM_ALL ../tests/test_${feature_source}.cpp ../tests/test_gpu_runtime_stubs.cpp)
    target_include_directories(${feature_test}-test PRIVATE . shim)
    target_compile_options(${feature_test}-test PRIVATE -UNDEBUG -fPIC -Wall -Wextra -Werror)
    if(feature_test STREQUAL "guest-hook-conditional")
        target_compile_options(${feature_test}-test PRIVATE -mno-red-zone)
    endif()
    target_link_libraries(${feature_test}-test PRIVATE bbgpu)
    list(APPEND CACHYBORNE_TEST_TARGETS ${feature_test}-test)
endforeach()

add_library(plugin-callback-module MODULE EXCLUDE_FROM_ALL ../tests/plugin_callback_module.c)
add_executable(plugin-callbacks-test EXCLUDE_FROM_ALL ../tests/test_plugin_callbacks.cpp ../tests/test_gpu_runtime_stubs.cpp)
target_include_directories(plugin-callbacks-test PRIVATE . shim)
target_compile_options(plugin-callbacks-test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
target_link_libraries(plugin-callbacks-test PRIVATE bbgpu ${CMAKE_DL_LIBS})
add_dependencies(plugin-callbacks-test plugin-callback-module)
list(APPEND CACHYBORNE_TEST_TARGETS plugin-callbacks-test)

add_library(plugin-module-fixture MODULE EXCLUDE_FROM_ALL ../tests/plugin_module_fixture.cpp)
target_include_directories(plugin-module-fixture PRIVATE shim)
add_executable(plugin-module-test EXCLUDE_FROM_ALL ../tests/test_plugin_module.cpp ../tests/test_gpu_runtime_stubs.cpp)
target_include_directories(plugin-module-test PRIVATE . shim)
target_compile_options(plugin-module-test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
target_link_libraries(plugin-module-test PRIVATE bbgpu ${CMAKE_DL_LIBS})
add_dependencies(plugin-module-test plugin-module-fixture plugin-callback-module)
list(APPEND CACHYBORNE_TEST_TARGETS plugin-module-test)
add_library(plugin-host-fixture MODULE EXCLUDE_FROM_ALL ../tests/plugin_host_fixture.cpp)
target_include_directories(plugin-host-fixture PRIVATE shim)
target_compile_options(plugin-host-fixture PRIVATE -UNDEBUG -Wall -Wextra -Werror)
add_library(plugin-host-retail MODULE EXCLUDE_FROM_ALL ../tests/plugin_host_retail.cpp)
target_include_directories(plugin-host-retail PRIVATE shim)
target_compile_options(plugin-host-retail PRIVATE -UNDEBUG -Wall -Wextra -Werror)
add_library(plugin-host-preload MODULE EXCLUDE_FROM_ALL ../tests/plugin_host_preload.cpp)
target_include_directories(plugin-host-preload PRIVATE . shim)
target_compile_options(plugin-host-preload PRIVATE -UNDEBUG -Wall -Wextra -Werror)
target_link_libraries(plugin-host-preload PRIVATE bbgpu ${CMAKE_DL_LIBS})
add_executable(plugin-host-test EXCLUDE_FROM_ALL ../tests/test_plugin_host.cpp ../tests/test_gpu_runtime_stubs.cpp)
target_include_directories(plugin-host-test PRIVATE . shim)
target_compile_options(plugin-host-test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
target_link_libraries(plugin-host-test PRIVATE bbgpu ${CMAKE_DL_LIBS})
add_dependencies(plugin-host-test plugin-host-fixture plugin-module-fixture plugin-host-retail plugin-host-preload)
list(APPEND CACHYBORNE_TEST_TARGETS plugin-host-test)
foreach(host_case IN ITEMS init-fail init-throw image-fail image-throw unknown-image version hook-throw service-close self-close)
    add_test(NAME plugin-host-${host_case} COMMAND plugin-host-test
        $<TARGET_FILE:plugin-host-fixture> $<TARGET_FILE:plugin-module-fixture> ${host_case})
    set_tests_properties(plugin-host-${host_case} PROPERTIES LABELS cachyborne TIMEOUT 30 SKIP_RETURN_CODE 77)
endforeach()
add_library(plugin-hook-fixture MODULE EXCLUDE_FROM_ALL ../tests/plugin_hook_fixture.cpp)
target_include_directories(plugin-hook-fixture PRIVATE shim)
target_compile_options(plugin-hook-fixture PRIVATE -UNDEBUG -Wall -Wextra -Werror)
add_executable(plugin-hooks-test EXCLUDE_FROM_ALL ../tests/test_plugin_hooks.cpp ../tests/test_gpu_runtime_stubs.cpp)
target_include_directories(plugin-hooks-test PRIVATE . shim)
target_compile_options(plugin-hooks-test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
target_link_libraries(plugin-hooks-test PRIVATE bbgpu ${CMAKE_DL_LIBS})
add_dependencies(plugin-hooks-test plugin-hook-fixture)
list(APPEND CACHYBORNE_TEST_TARGETS plugin-hooks-test)
add_library(plugin-hle-integration MODULE EXCLUDE_FROM_ALL ../tests/plugin_hle_integration.cpp)
target_include_directories(plugin-hle-integration PRIVATE . shim)
target_compile_options(plugin-hle-integration PRIVATE -UNDEBUG -Wall -Wextra -Werror)
target_link_libraries(plugin-hle-integration PRIVATE bbgpu)
add_dependencies(plugin-module-test plugin-hle-integration)
foreach(hle_case IN ITEMS hle-init-fail hle-image-fail hle-success hle-frame-throw)
    add_test(NAME plugin-native-${hle_case} COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/../tests/run_plugin_hle_integration.py
        ${CMAKE_BINARY_DIR}/../runtime-test $<TARGET_FILE:plugin-hle-integration> $<TARGET_FILE:plugin-module-fixture> ${hle_case})
    set_tests_properties(plugin-native-${hle_case} PROPERTIES LABELS cachyborne TIMEOUT 30 SKIP_RETURN_CODE 77)
endforeach()

foreach(host_hle_case IN ITEMS host-hle-success host-hle-init-fail)
    add_test(NAME plugin-native-${host_hle_case} COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/../tests/run_plugin_hle_integration.py
        ${CMAKE_BINARY_DIR}/../runtime-test $<TARGET_FILE:plugin-hle-integration> $<TARGET_FILE:plugin-host-fixture> ${host_hle_case})
    set_tests_properties(plugin-native-${host_hle_case} PROPERTIES LABELS cachyborne TIMEOUT 30 SKIP_RETURN_CODE 77)
endforeach()

add_executable(reserved-formats-test EXCLUDE_FROM_ALL ../tests/test_reserved_formats.cpp ../tests/test_gpu_runtime_stubs.cpp)
target_include_directories(reserved-formats-test PRIVATE . shim ${SHADER_INCLUDE} third_party/gcn/include)
target_compile_options(reserved-formats-test PRIVATE -UNDEBUG -fPIC -Wall -Wextra -Werror
    -Wno-reorder-ctor -Wno-unused-parameter) # Existing shader/info.h diagnostics.
target_link_libraries(reserved-formats-test PRIVATE bbgpu)

if(TARGET bbnet20)
    add_executable(thd-datagrams-test EXCLUDE_FROM_ALL ../tests/test_thd_datagrams.cpp ../tests/test_gpu_runtime_stubs.cpp)
    target_include_directories(thd-datagrams-test PRIVATE .)
    target_compile_options(thd-datagrams-test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
    target_link_options(thd-datagrams-test PRIVATE -Wl,--export-dynamic)
    # Match bb-probe: bbgpu's initial-exec TLS must exist at process startup,
    # even though the socket test only reaches it through dlopen(bbnet20).
    target_link_libraries(thd-datagrams-test PRIVATE "-Wl,--no-as-needed" bbgpu
        "-Wl,--as-needed" ${CMAKE_DL_LIBS})
    add_dependencies(thd-datagrams-test bbnet20)
    list(APPEND CACHYBORNE_TEST_TARGETS thd-datagrams-test)
endif()

add_executable(guest-memory-retirement-test EXCLUDE_FROM_ALL ../tests/test_guest_memory_retirement.cpp ../tests/test_gpu_runtime_stubs.cpp)
target_include_directories(guest-memory-retirement-test PRIVATE . shim layer ${SHADER_INCLUDE} third_party/gcn/include)
target_compile_options(guest-memory-retirement-test PRIVATE -UNDEBUG -fPIC -Wall -Wextra -Werror)
target_link_libraries(guest-memory-retirement-test PRIVATE bbgpu)

find_package(ZLIB REQUIRED)
add_executable(guest-deflate-test EXCLUDE_FROM_ALL ../tests/test_guest_deflate.cpp games/bloodborne/guest_deflate.cpp)
target_include_directories(guest-deflate-test PRIVATE .)
target_compile_options(guest-deflate-test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
target_link_libraries(guest-deflate-test PRIVATE PkgConfig::LIBDEFLATE ZLIB::ZLIB)

add_executable(contact-shadows-test EXCLUDE_FROM_ALL ../tests/test_contact_shadows.cpp ../tests/test_gpu_runtime_stubs.cpp)
target_include_directories(contact-shadows-test PRIVATE . shim ${SHADER_INCLUDE} third_party/gcn/include)
target_compile_options(contact-shadows-test PRIVATE -UNDEBUG -fPIC)
target_link_libraries(contact-shadows-test PRIVATE bbgpu)

add_executable(latency-test EXCLUDE_FROM_ALL ../tests/test_latency.cpp ../tests/test_gpu_runtime_stubs.cpp)
target_include_directories(latency-test PRIVATE . shim ${SHADER_INCLUDE} third_party/gcn/include)
target_compile_options(latency-test PRIVATE -UNDEBUG -fPIC)
target_link_libraries(latency-test PRIVATE bbgpu)

list(APPEND CACHYBORNE_TEST_TARGETS record-data-test record-chunk-test hybrid-affinity-test staging-pool-test copy-filter-test reserved-formats-test guest-memory-retirement-test guest-deflate-test contact-shadows-test latency-test)
add_custom_target(cachyborne-tests DEPENDS ${CACHYBORNE_TEST_TARGETS})
foreach(test_target IN LISTS CACHYBORNE_TEST_TARGETS)
    target_compile_options(${test_target} PRIVATE -UNDEBUG)
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/test-output/${test_target}")
    if(test_target STREQUAL "shader-corpus-test")
        # No recorded corpus is required for the deterministic auxiliary hash regression.
        add_test(NAME ${test_target} COMMAND ${test_target} --aux)
    elseif(test_target STREQUAL "thd-datagrams-test")
        add_test(NAME ${test_target} COMMAND ${test_target} $<TARGET_FILE:bbnet20>)
    elseif(test_target STREQUAL "plugin-callbacks-test")
        add_test(NAME ${test_target} COMMAND ${test_target} $<TARGET_FILE:plugin-callback-module>)
    elseif(test_target STREQUAL "plugin-module-test")
        add_test(NAME ${test_target} COMMAND ${test_target} $<TARGET_FILE:plugin-module-fixture> $<TARGET_FILE:plugin-callback-module>)
    elseif(test_target STREQUAL "plugin-host-test")
        add_test(NAME ${test_target} COMMAND ${test_target} $<TARGET_FILE:plugin-host-fixture> $<TARGET_FILE:plugin-module-fixture> success)
    elseif(test_target STREQUAL "plugin-hooks-test")
        add_test(NAME ${test_target} COMMAND ${test_target} $<TARGET_FILE:plugin-hook-fixture>)
    else()
        add_test(NAME ${test_target} COMMAND ${test_target})
    endif()
    set_tests_properties(${test_target} PROPERTIES
        LABELS cachyborne TIMEOUT 120
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/test-output/${test_target}")
endforeach()

set_tests_properties(gx-job-barrier-test chr-sync-test chr-replay_manipulator-test ragdoll-clock-test frame-clock-hooks-test PROPERTIES SKIP_RETURN_CODE 77)
set_tests_properties(guest-memory-retirement-test PROPERTIES SKIP_RETURN_CODE 77)
set_tests_properties(limiter-hooks-test PROPERTIES SKIP_RETURN_CODE 77)
set_tests_properties(rebirth-price-test PROPERTIES SKIP_RETURN_CODE 77)
set_tests_properties(plugin-callbacks-frame-test PROPERTIES SKIP_RETURN_CODE 77)
set_tests_properties(plugin-image-test PROPERTIES SKIP_RETURN_CODE 77)
set_tests_properties(plugin-hooks-test PROPERTIES SKIP_RETURN_CODE 77)
foreach(margin IN ITEMS 0 2000 5000)
    add_test(NAME limiter-fixed-${margin}-test COMMAND limiter-hooks-test ${margin})
    set_tests_properties(limiter-fixed-${margin}-test PROPERTIES LABELS cachyborne TIMEOUT 120 SKIP_RETURN_CODE 77)
endforeach()
add_test(NAME limiter-core-hooks-test COMMAND limiter-hooks-test -1 --core)
set_tests_properties(limiter-core-hooks-test PROPERTIES LABELS cachyborne TIMEOUT 120 SKIP_RETURN_CODE 77)
foreach(site RANGE 0 7)
    add_test(NAME limiter-mismatch-${site}-test COMMAND limiter-hooks-test mismatch ${site})
    set_tests_properties(limiter-mismatch-${site}-test PROPERTIES LABELS cachyborne TIMEOUT 120 SKIP_RETURN_CODE 77)
endforeach()
set_tests_properties(sound-wake-hooks-test PROPERTIES SKIP_RETURN_CODE 77)
if(TARGET bbnet20)
    # These tests use only loopback, no game dump, account or session server.
    foreach(mode IN ITEMS whole legacy)
        add_test(NAME thd-datagrams-${mode}-test COMMAND thd-datagrams-test $<TARGET_FILE:bbnet20>)
        set_tests_properties(thd-datagrams-${mode}-test PROPERTIES LABELS cachyborne TIMEOUT 30)
    endforeach()
    set_tests_properties(thd-datagrams-test PROPERTIES ENVIRONMENT "BB_NET_LARGE_DATAGRAMS=1;BB_NET_SPLIT_DATAGRAMS=1")
    set_tests_properties(thd-datagrams-whole-test PROPERTIES ENVIRONMENT "BB_NET_LARGE_DATAGRAMS=1;BB_NET_SPLIT_DATAGRAMS=0")
    set_tests_properties(thd-datagrams-legacy-test PROPERTIES ENVIRONMENT "BB_NET_LARGE_DATAGRAMS=0;BB_NET_SPLIT_DATAGRAMS=1")
endif()
set_tests_properties(occlusion-fresh-test PROPERTIES SKIP_RETURN_CODE 77)
foreach(site RANGE 0 3)
    add_test(NAME occlusion-fresh-mismatch-${site}-test COMMAND occlusion-fresh-test ${site})
    set_tests_properties(occlusion-fresh-mismatch-${site}-test PROPERTIES LABELS cachyborne TIMEOUT 120 SKIP_RETURN_CODE 77)
endforeach()
set_tests_properties(frame-tasks-test menu-steps-test appearance-test rebirth-test fmod-project-init-test sf-text-layout-test gx-rename-retail-test gx-reclaim-test save-quit-test PROPERTIES SKIP_RETURN_CODE 77)
add_test(NAME gx-reclaim-off-test COMMAND gx-reclaim-test --off)
set_tests_properties(gx-reclaim-off-test PROPERTIES LABELS cachyborne TIMEOUT 120 SKIP_RETURN_CODE 77)
set_tests_properties(gx-reclaim-test gx-reclaim-off-test PROPERTIES ENVIRONMENT "BB_FRAME_STATS=1")

add_test(NAME frame-clock-hooks-off-test COMMAND frame-clock-hooks-test --off)
set_tests_properties(frame-clock-hooks-off-test PROPERTIES LABELS cachyborne TIMEOUT 120 SKIP_RETURN_CODE 77)
add_test(NAME wind-pace-off-test COMMAND frame-clock-hooks-test --off --wind-off)
set_tests_properties(wind-pace-off-test PROPERTIES LABELS cachyborne TIMEOUT 120 SKIP_RETURN_CODE 77)
add_test(NAME wind-pace-mismatch-test COMMAND frame-clock-hooks-test --off --wind-off --wind-mismatch)
set_tests_properties(wind-pace-mismatch-test PROPERTIES LABELS cachyborne TIMEOUT 120 SKIP_RETURN_CODE 77)

set_tests_properties(water-gate-test PROPERTIES SKIP_RETURN_CODE 77)
foreach(site IN ITEMS 0 1)
    add_test(NAME water-gate-mismatch-${site}-test COMMAND water-gate-test ${site})
    set_tests_properties(water-gate-mismatch-${site}-test PROPERTIES LABELS cachyborne TIMEOUT 120 SKIP_RETURN_CODE 77)
endforeach()

# Capture final target/directory settings, excluding the text of unrelated test
# declarations. Each configuration gets its own generated identity header.
bb_target_shader_fingerprint(bbgpu "${CMAKE_CURRENT_SOURCE_DIR}"
    "${SHADER_INCLUDE}/$<CONFIG>/bbgpu/shader_build.h"
    SOURCES ${BB_SHADER_FINGERPRINT_SOURCES} DEPENDENCY_TARGETS sirit)
