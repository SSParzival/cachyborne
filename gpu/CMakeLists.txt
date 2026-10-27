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
find_program(GLSLANG_COMPILER NAMES glslang glslangValidator REQUIRED)

set(SIRIT_USE_SYSTEM_SPIRV_HEADERS OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/sirit EXCLUDE_FROM_ALL)

# bbport: FSR 3.1 temporal upscaling / frame generation, native Vulkan (MIT, FireBurn/FSR-Vulkan).
set(FFX_VK_PORTABLE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FFX_VK_PORTABLE_BUILD_PROBE OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/fsr-vulkan EXCLUDE_FROM_ALL)

# Host shaders -> headers: SPIR-V arrays from glslang, in bbgpu/shaders/.
set(SHADER_INCLUDE ${CMAKE_CURRENT_BINARY_DIR}/include)
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
add_own_shader(upscale/shaders/upscale_merge.comp upscale_merge_comp)
add_own_shader(upscale/shaders/upscale_reactive.comp upscale_reactive_comp)
add_own_shader(upscale/shaders/taa.comp taa_comp)
add_own_shader(upscale/shaders/taa_sharpen.comp taa_sharpen_comp)
add_own_shader(upscale/shaders/taa_sharpen.comp taa_sharpen_ldr_comp LDR_OUTPUT=1)
add_own_shader(upscale/shaders/fsr4_color.comp fsr4_decode_comp)
add_own_shader(upscale/shaders/fsr4_color.comp fsr4_encode_comp ENCODE=1)
add_own_shader(upscale/shaders/fsr4_reactive.comp fsr4_reactive_comp)
add_own_shader(render/shaders/buffer_multi_copy.comp buffer_multi_copy_comp)
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
# Own system libraries (Gnm, VideoOut, AvPlayer, event queues, interrupts): platform/.
file(GLOB_RECURSE PLATFORM_SOURCES CONFIGURE_DEPENDS platform/*.cpp)
# platform/win32: the POSIX subset over Win32 (with its headers in platform/win32/include).
    list(FILTER PLATFORM_SOURCES EXCLUDE REGEX "/platform/win32/")
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil libswscale libswresample)
file(GLOB SHIM_SOURCES CONFIGURE_DEPENDS shim/*.cpp shim/*/*.cpp)
# The layer's own modules (independent of the shadPS4 code): layer/.
file(GLOB LAYER_SOURCES CONFIGURE_DEPENDS layer/*.cpp)
# Game profiles (what the translator knows about one game, out of its core): games/.
file(GLOB GAME_SOURCES CONFIGURE_DEPENDS games/*.cpp)
# The translator's own command processor (docs/TRANSLATOR_ARCHITECTURE.ru.md, step 2): cp/.
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
target_include_directories(bbgpu PRIVATE . shim layer games cp ${SHADER_INCLUDE} third_party/gcn/include third_party/half/include)
target_compile_definitions(bbgpu PRIVATE BOOST_ASIO_STANDALONE NDEBUG)
target_compile_options(bbgpu PRIVATE -Wno-deprecated-declarations -Wno-missing-field-initializers)
# Loaded with bb-probe at startup (DT_NEEDED, never dlopen): thread_local access without a
# TLS descriptor call (_dl_tlsdesc_return was ~2% of the draw recording thread).
    target_compile_options(bbgpu PRIVATE -ftls-model=initial-exec)
    # Runtime symbols (runtime_memory_*, runtime_process_*) resolve against bb-probe.
    target_link_options(bbgpu PRIVATE -Wl,--allow-shlib-undefined)
target_link_libraries(bbgpu PRIVATE ffx-vulkan::portable ffx-vulkan::fsr4-v07-vulkan)
target_link_libraries(bbgpu PUBLIC Vulkan::Headers fmt::fmt Boost::headers magic_enum::magic_enum
    tsl::robin_map GPUOpen::VulkanMemoryAllocator PkgConfig::XXHASH PkgConfig::SDL3 PkgConfig::FFMPEG miniz::miniz Zydis::Zydis sirit)
    target_link_libraries(bbgpu PUBLIC PkgConfig::X11)

# Explicit test targets; neither launches the game nor needs a Vulkan device.
add_executable(motion-history-test EXCLUDE_FROM_ALL ../tests/test_motion_history.cpp)
target_compile_options(motion-history-test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
target_include_directories(motion-history-test PRIVATE ..)
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
target_include_directories(settings-test PRIVATE .. shim)

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

# bbport: FSR 4 benchmark outside the game (tools/fsr4_bench.cpp): ninja -C out/gpu fsr4-bench.
add_executable(upscaler-support-test EXCLUDE_FROM_ALL ../tests/test_upscaler_support.cpp
    shim/bbport_settings.cpp)
target_include_directories(upscaler-support-test PRIVATE shim)
target_compile_options(upscaler-support-test PRIVATE -UNDEBUG)

add_executable(fsr4-bench EXCLUDE_FROM_ALL tools/fsr4_bench.cpp
    upscale/fsr411/fsr411.cpp)
target_include_directories(fsr4-bench PRIVATE upscale/fsr411)
target_link_libraries(fsr4-bench PRIVATE ffx-vulkan::fsr4-v07-vulkan ffx-vulkan::fsr4-v07-assets Vulkan::Vulkan)

# bbport online module (gpu/bbnet, libbbnet.so): built when its libraries are there (shell.nix);
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
        add_subdirectory(bbnet)
        add_subdirectory(bbnet20)
        message(STATUS "Online module: on (libbbnet.so)")
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

# All project-owned regression targets are explicit, including assertions in Release builds.
enable_testing()
set(CACHYBORNE_TEST_TARGETS spanish-game-menu-test motion-history-test motion-shader-test ui-composition-test
    cp-decoder-test fx-init-clear-test effect-flags-test settings-test scene-resolution-test
    taa-shader-test camera-motion-test translation-memory-test upscaler-support-test
    shader-corpus-test srt-walker-fault-test)
add_custom_target(cachyborne-tests DEPENDS ${CACHYBORNE_TEST_TARGETS})
foreach(test_target IN LISTS CACHYBORNE_TEST_TARGETS)
    target_compile_options(${test_target} PRIVATE -UNDEBUG)
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/test-output/${test_target}")
    if(test_target STREQUAL "shader-corpus-test")
        # No recorded corpus is required for the deterministic auxiliary hash regression.
        add_test(NAME ${test_target} COMMAND ${test_target} --aux)
    else()
        add_test(NAME ${test_target} COMMAND ${test_target})
    endif()
    set_tests_properties(${test_target} PROPERTIES
        LABELS cachyborne TIMEOUT 120
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/test-output/${test_target}")
endforeach()
