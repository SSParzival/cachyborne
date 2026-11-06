// SPDX-License-Identifier: GPL-3.0-or-later
#include "sf_text_layout.h"

#include <sys/mman.h>
#include <unistd.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace BbSfTextLayout {
namespace {

using UpdateTransformFn = void (*)(void* self, void* hal, const void* node_data,
                                   const void* transform_args, uint32_t flags);
using HandleChangesFn = void (*)(void* self, void* hal, uint32_t change_bits);

// Instructions 1..8 at 0x12d5e0..0x12d5f3 (20 bytes, no RIP-relative instructions):
constexpr uint8_t kExpectedUpdateTransform20[20] = {
    0x55,                                            // push %rbp
    0x48, 0x89, 0xe5,                                // mov %rsp, %rbp
    0x41, 0x57,                                      // push %r15
    0x41, 0x56,                                      // push %r14
    0x41, 0x55,                                      // push %r13
    0x41, 0x54,                                      // push %r12
    0x53,                                            // push %rbx
    0x48, 0x81, 0xe4, 0xe0, 0xff, 0xff, 0xff,        // and $-32, %rsp
};

// Instructions 1..8 at 0x12df80..0x12df8e (15 bytes, no RIP-relative instructions):
constexpr uint8_t kExpectedHandleChanges15[15] = {
    0x55,              // push %rbp
    0x48, 0x89, 0xe5,  // mov %rsp, %rbp
    0x41, 0x57,        // push %r15
    0x41, 0x56,        // push %r14
    0x53,              // push %rbx
    0x50,              // push %rax
    0x89, 0xd3,        // mov %edx, %ebx
    0x49, 0x89, 0xf6,  // mov %rsi, %r14
};

constexpr uint8_t kSceneCheck1Orig[2] = {0x74, 0x36};
constexpr uint8_t kSceneCheck1Patch[2] = {0x75, 0x11};
constexpr uint8_t kSceneCheck2Orig[2] = {0x74, 0x33};
constexpr uint8_t kSceneCheck2Patch[2] = {0x75, 0x12};

constexpr uint8_t kWaterWaveOrig[4] = {
    0xd1, 0xee,  // shr $1, %esi
    0xd1, 0xea,  // shr $1, %edx
};
constexpr uint8_t kWaterWavePatch[4] = {
    0x90, 0x90, 0x90, 0x90,  // nop; nop; nop; nop
};

constexpr uint8_t kMatchingRegionOrig[4] = {0xc6, 0x43, 0x0f, 0x00};
constexpr uint8_t kMatchingRegionPatch[4] = {0xc6, 0x43, 0x0f, 0x01};

struct AtomicStats {
    std::atomic<uint64_t> layout_null_skips{0};
    std::atomic<uint64_t> layout_requeued{0};
    std::atomic<uint64_t> layout_change_events{0};
    std::atomic<bool> guard_installed{false};
    std::atomic<bool> scene_both_sides_patched{false};
    std::atomic<bool> water_wave_full_patched{false};
    std::atomic<bool> matching_worldwide_patched{false};
    std::atomic<bool> sign_timeout_patched{false};
    std::atomic<float> sign_timeout_seconds{180.0f};
};

struct State {
    uint8_t* image = nullptr;
    size_t size = 0;
    void* trampoline_page = nullptr;
    size_t trampoline_size = 0;
    UpdateTransformFn orig_update_transform = nullptr;
    HandleChangesFn orig_handle_changes = nullptr;
};

AtomicStats g_stats;
State g_state;

bool EnvDisabled(const char* name) {
    const char* v = std::getenv(name);
    return v && (std::strcmp(v, "0") == 0 || std::strcmp(v, "off") == 0 ||
                 std::strcmp(v, "false") == 0);
}

bool EnvEnabled(const char* name) {
    const char* v = std::getenv(name);
    return v && (std::strcmp(v, "1") == 0 || std::strcmp(v, "on") == 0 ||
                 std::strcmp(v, "true") == 0);
}

bool MakeWritable(void* addr, size_t len, int prot) {
    const size_t page = static_cast<size_t>(sysconf(_SC_PAGESIZE));
    const uintptr_t start = reinterpret_cast<uintptr_t>(addr) & ~(page - 1);
    const uintptr_t end =
        (reinterpret_cast<uintptr_t>(addr) + len + page - 1) & ~(page - 1);
    return mprotect(reinterpret_cast<void*>(start), end - start, prot) == 0;
}

void WriteAbsJump(uint8_t* dst, const void* target, size_t pad_len) {
    dst[0] = 0x48;
    dst[1] = 0xb8;  // movabs $target, %rax
    const uint64_t addr = reinterpret_cast<uint64_t>(target);
    std::memcpy(dst + 2, &addr, sizeof(addr));
    dst[10] = 0xff;
    dst[11] = 0xe0;  // jmp *%rax
    for (size_t i = 12; i < pad_len; ++i) dst[i] = 0x90;
}

bool QueueDeferredLayoutUpdate(void* self) {
    if (!self) return false;
    auto* bytes = static_cast<uint8_t*>(self);
    void* root = nullptr;
    std::memcpy(&root, bytes + kTreeCacheRootOffset, sizeof(root));
    if (!root) return false;

    auto* root_bytes = static_cast<uint8_t*>(root);
    uint8_t depth_updates_chained = 0;
    std::memcpy(&depth_updates_chained,
                root_bytes + kTreeRootDepthUpdatesChainedOffset,
                sizeof(depth_updates_chained));
    if (depth_updates_chained != 0) return false;

    uint32_t update_flags = 0;
    std::memcpy(&update_flags, bytes + kTreeCacheUpdateFlagsOffset,
                sizeof(update_flags));
    if ((update_flags & kUpdateInListFlag) == 0) {
        void* prev_head = nullptr;
        std::memcpy(&prev_head, root_bytes + kTreeRootUpdateListOffset,
                    sizeof(prev_head));
        std::memcpy(bytes + kTreeCacheNextUpdateOffset, &prev_head,
                    sizeof(prev_head));
        std::memcpy(root_bytes + kTreeRootUpdateListOffset, &self,
                    sizeof(self));
        update_flags |= kUpdateInListFlag;
    }
    update_flags |= kUpdateLayoutFlag;
    std::memcpy(bytes + kTreeCacheUpdateFlagsOffset, &update_flags,
                sizeof(update_flags));
    g_stats.layout_requeued.fetch_add(1, std::memory_order_relaxed);
    return true;
}

void UpdateTransformHook(void* self, void* hal, const void* node_data,
                         const void* transform_args, uint32_t flags) {
    if ((flags & kUpdateLayoutPart) != 0 && self != nullptr &&
        node_data != nullptr) {
        const void* layout = nullptr;
        std::memcpy(
            &layout,
            static_cast<const uint8_t*>(node_data) + kNodeDataLayoutOffset,
            sizeof(layout));
        if (layout == nullptr) {
            const uint64_t n = g_stats.layout_null_skips.fetch_add(
                                   1, std::memory_order_relaxed) +
                               1;
            QueueDeferredLayoutUpdate(self);
            if (n <= 8 || (n & (n - 1)) == 0) {
                std::fprintf(stderr,
                             "[bb-sf-text-layout] skipped null Text::Layout* "
                             "in TreeCacheText::UpdateTransform (self=%p, "
                             "node_data=%p, count=%llu)\n",
                             self, node_data,
                             static_cast<unsigned long long>(n));
            }
            if (g_state.orig_update_transform) {
                g_state.orig_update_transform(self, hal, node_data,
                                              transform_args,
                                              flags & ~kUpdateLayoutPart);
            }
            return;
        }
    }
    if (g_state.orig_update_transform) {
        g_state.orig_update_transform(self, hal, node_data, transform_args,
                                      flags);
    }
}

void HandleChangesHook(void* self, void* hal, uint32_t change_bits) {
    if ((change_bits & kChangeTextLayout) != 0) {
        g_stats.layout_change_events.fetch_add(1, std::memory_order_relaxed);
    }
    if (g_state.orig_handle_changes) {
        g_state.orig_handle_changes(self, hal, change_bits);
    }
}

void InstallScaleformGuard(uint8_t* image, size_t size) {
    if (EnvDisabled("BB_SF_TEXT_GUARD")) return;
    if (size < kHandleChangesVaddr + sizeof(kExpectedHandleChanges15)) return;

    uint8_t* update_site = image + kUpdateTransformVaddr;
    uint8_t* changes_site = image + kHandleChangesVaddr;
    if (std::memcmp(update_site, kExpectedUpdateTransform20,
                    sizeof(kExpectedUpdateTransform20)) != 0 ||
        std::memcmp(changes_site, kExpectedHandleChanges15,
                    sizeof(kExpectedHandleChanges15)) != 0) {
        return;
    }

    const size_t page = static_cast<size_t>(sysconf(_SC_PAGESIZE));
    void* tramp = mmap(nullptr, page, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (tramp == MAP_FAILED) return;

    auto* t0 = static_cast<uint8_t*>(tramp);
    std::memcpy(t0, update_site, sizeof(kExpectedUpdateTransform20));
    WriteAbsJump(t0 + sizeof(kExpectedUpdateTransform20),
                 update_site + sizeof(kExpectedUpdateTransform20), 12);

    auto* t1 = t0 + 64;
    std::memcpy(t1, changes_site, sizeof(kExpectedHandleChanges15));
    WriteAbsJump(t1 + sizeof(kExpectedHandleChanges15),
                 changes_site + sizeof(kExpectedHandleChanges15), 12);

    if (mprotect(tramp, page, PROT_READ | PROT_EXEC) != 0) {
        munmap(tramp, page);
        return;
    }

    if (!MakeWritable(update_site, sizeof(kExpectedUpdateTransform20),
                      PROT_READ | PROT_WRITE | PROT_EXEC) ||
        !MakeWritable(changes_site, sizeof(kExpectedHandleChanges15),
                      PROT_READ | PROT_WRITE | PROT_EXEC)) {
        munmap(tramp, page);
        return;
    }

    g_state.trampoline_page = tramp;
    g_state.trampoline_size = page;
    g_state.orig_update_transform = reinterpret_cast<UpdateTransformFn>(t0);
    g_state.orig_handle_changes = reinterpret_cast<HandleChangesFn>(t1);

    WriteAbsJump(update_site, reinterpret_cast<const void*>(&UpdateTransformHook),
                 sizeof(kExpectedUpdateTransform20));
    WriteAbsJump(changes_site, reinterpret_cast<const void*>(&HandleChangesHook),
                 sizeof(kExpectedHandleChanges15));
    __builtin___clear_cache(reinterpret_cast<char*>(update_site),
                            reinterpret_cast<char*>(update_site + sizeof(kExpectedUpdateTransform20)));
    __builtin___clear_cache(reinterpret_cast<char*>(changes_site),
                            reinterpret_cast<char*>(changes_site + sizeof(kExpectedHandleChanges15)));
    g_stats.guard_installed.store(true, std::memory_order_relaxed);
}

void InstallSceneContextFixes(uint8_t* image, size_t size) {
    if (!EnvDisabled("BB_SCENE_BOTH_SIDES") &&
        size >= kSceneCheck2Vaddr + sizeof(kSceneCheck2Orig)) {
        uint8_t* c1 = image + kSceneCheck1Vaddr;
        uint8_t* c2 = image + kSceneCheck2Vaddr;
        const bool orig_match =
            std::memcmp(c1, kSceneCheck1Orig, sizeof(kSceneCheck1Orig)) == 0 &&
            std::memcmp(c2, kSceneCheck2Orig, sizeof(kSceneCheck2Orig)) == 0;
        const bool already_patched =
            std::memcmp(c1, kSceneCheck1Patch, sizeof(kSceneCheck1Patch)) == 0 &&
            std::memcmp(c2, kSceneCheck2Patch, sizeof(kSceneCheck2Patch)) == 0;
        if (orig_match) {
            if (MakeWritable(c1, sizeof(kSceneCheck1Patch),
                             PROT_READ | PROT_WRITE | PROT_EXEC) &&
                MakeWritable(c2, sizeof(kSceneCheck2Patch),
                             PROT_READ | PROT_WRITE | PROT_EXEC)) {
                std::memcpy(c1, kSceneCheck1Patch, sizeof(kSceneCheck1Patch));
                std::memcpy(c2, kSceneCheck2Patch, sizeof(kSceneCheck2Patch));
                g_stats.scene_both_sides_patched.store(
                    true, std::memory_order_relaxed);
            }
        } else if (already_patched) {
            g_stats.scene_both_sides_patched.store(true,
                                                   std::memory_order_relaxed);
        }
    }

    if (EnvEnabled("BB_WATER_WAVE_FULL") &&
        size >= kWaterWaveDivVaddr + sizeof(kWaterWaveOrig)) {
        uint8_t* wave = image + kWaterWaveDivVaddr;
        if (std::memcmp(wave, kWaterWaveOrig, sizeof(kWaterWaveOrig)) == 0) {
            if (MakeWritable(wave, sizeof(kWaterWavePatch),
                             PROT_READ | PROT_WRITE | PROT_EXEC)) {
                std::memcpy(wave, kWaterWavePatch, sizeof(kWaterWavePatch));
                g_stats.water_wave_full_patched.store(
                    true, std::memory_order_relaxed);
            }
        } else if (std::memcmp(wave, kWaterWavePatch,
                               sizeof(kWaterWavePatch)) == 0) {
            g_stats.water_wave_full_patched.store(true,
                                                  std::memory_order_relaxed);
        }
    }
}

void InstallNetworkQoLFixes(uint8_t* image, size_t size) {
    const char* region = std::getenv("BB_MATCHING_REGION");
    const bool keep_local =
        region && (std::strcmp(region, "0") == 0 ||
                   std::strcmp(region, "local") == 0 ||
                   std::strcmp(region, "game") == 0);
    if (!keep_local &&
        size >= kMatchingRegionVaddr + sizeof(kMatchingRegionOrig)) {
        uint8_t* site = image + kMatchingRegionVaddr;
        if (std::memcmp(site, kMatchingRegionOrig,
                        sizeof(kMatchingRegionOrig)) == 0) {
            if (MakeWritable(site, sizeof(kMatchingRegionPatch),
                             PROT_READ | PROT_WRITE | PROT_EXEC)) {
                std::memcpy(site, kMatchingRegionPatch,
                            sizeof(kMatchingRegionPatch));
                g_stats.matching_worldwide_patched.store(
                    true, std::memory_order_relaxed);
            }
        } else if (std::memcmp(site, kMatchingRegionPatch,
                               sizeof(kMatchingRegionPatch)) == 0) {
            g_stats.matching_worldwide_patched.store(true,
                                                     std::memory_order_relaxed);
        }
    }

    const char* timeout_env = std::getenv("BB_SIGN_TIMEOUT");
    if (timeout_env && *timeout_env &&
        size >= kSignTimeoutVaddr + sizeof(float)) {
        char* end = nullptr;
        const float seconds = std::strtof(timeout_env, &end);
        if (end != timeout_env && *end == '\0' && std::isfinite(seconds) &&
            seconds > 0.0f && seconds <= 86400.0f) {
            uint8_t* site = image + kSignTimeoutVaddr;
            if (MakeWritable(site, sizeof(float),
                             PROT_READ | PROT_WRITE)) {
                std::memcpy(site, &seconds, sizeof(seconds));
                g_stats.sign_timeout_patched.store(true,
                                                   std::memory_order_relaxed);
                g_stats.sign_timeout_seconds.store(seconds,
                                                   std::memory_order_relaxed);
            }
        }
    }
}

}  // namespace

void Install(uint8_t* image, size_t size) {
    if (!image) return;
    g_state.image = image;
    g_state.size = size;
    InstallScaleformGuard(image, size);
    InstallSceneContextFixes(image, size);
    InstallNetworkQoLFixes(image, size);
}

void ResetForTests() {
    if (g_state.image && g_state.orig_update_transform &&
        g_state.size >= kUpdateTransformVaddr + sizeof(kExpectedUpdateTransform20)) {
        uint8_t* update_site = g_state.image + kUpdateTransformVaddr;
        if (MakeWritable(update_site, sizeof(kExpectedUpdateTransform20),
                         PROT_READ | PROT_WRITE | PROT_EXEC)) {
            std::memcpy(update_site, kExpectedUpdateTransform20,
                        sizeof(kExpectedUpdateTransform20));
        }
    }
    if (g_state.image && g_state.orig_handle_changes &&
        g_state.size >= kHandleChangesVaddr + sizeof(kExpectedHandleChanges15)) {
        uint8_t* changes_site = g_state.image + kHandleChangesVaddr;
        if (MakeWritable(changes_site, sizeof(kExpectedHandleChanges15),
                         PROT_READ | PROT_WRITE | PROT_EXEC)) {
            std::memcpy(changes_site, kExpectedHandleChanges15,
                        sizeof(kExpectedHandleChanges15));
        }
    }
    if (g_state.trampoline_page && g_state.trampoline_size) {
        munmap(g_state.trampoline_page, g_state.trampoline_size);
    }
    g_state = State{};
    g_stats.layout_null_skips.store(0, std::memory_order_relaxed);
    g_stats.layout_requeued.store(0, std::memory_order_relaxed);
    g_stats.layout_change_events.store(0, std::memory_order_relaxed);
    g_stats.guard_installed.store(false, std::memory_order_relaxed);
    g_stats.scene_both_sides_patched.store(false, std::memory_order_relaxed);
    g_stats.water_wave_full_patched.store(false, std::memory_order_relaxed);
    g_stats.matching_worldwide_patched.store(false, std::memory_order_relaxed);
    g_stats.sign_timeout_patched.store(false, std::memory_order_relaxed);
    g_stats.sign_timeout_seconds.store(180.0f, std::memory_order_relaxed);
}

Stats GetStats() {
    return Stats{
        g_stats.layout_null_skips.load(std::memory_order_relaxed),
        g_stats.layout_requeued.load(std::memory_order_relaxed),
        g_stats.layout_change_events.load(std::memory_order_relaxed),
        g_stats.guard_installed.load(std::memory_order_relaxed),
        g_stats.scene_both_sides_patched.load(std::memory_order_relaxed),
        g_stats.water_wave_full_patched.load(std::memory_order_relaxed),
        g_stats.matching_worldwide_patched.load(std::memory_order_relaxed),
        g_stats.sign_timeout_patched.load(std::memory_order_relaxed),
        g_stats.sign_timeout_seconds.load(std::memory_order_relaxed),
    };
}

void UpdateTransformForTest(void* self, void* hal, const void* node_data,
                            const void* transform_args, uint32_t flags) {
    UpdateTransformHook(self, hal, node_data, transform_args, flags);
}

void HandleChangesForTest(void* self, void* hal, uint32_t change_bits) {
    HandleChangesHook(self, hal, change_bits);
}

}  // namespace BbSfTextLayout
