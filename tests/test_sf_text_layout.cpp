// SPDX-License-Identifier: GPL-3.0-or-later
#undef NDEBUG
#include "retail_timing_kit.h"
#include "games/bloodborne/sf_text_layout.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

int main() {
    if (!eboot_kit::load()) return 77;

    constexpr size_t kImageSize = 0x55d8000;
    uint8_t* const image = eboot_kit::image;

    // Preserve original retail bytes around the patched sites for clean test resets.
    uint8_t orig_scene1[2];
    uint8_t orig_scene2[2];
    uint8_t orig_water[4];
    uint8_t orig_region[4];
    uint8_t orig_timeout[4];
    std::memcpy(orig_scene1, image + BbSfTextLayout::kSceneCheck1Vaddr, sizeof(orig_scene1));
    std::memcpy(orig_scene2, image + BbSfTextLayout::kSceneCheck2Vaddr, sizeof(orig_scene2));
    std::memcpy(orig_water, image + BbSfTextLayout::kWaterWaveDivVaddr, sizeof(orig_water));
    std::memcpy(orig_region, image + BbSfTextLayout::kMatchingRegionVaddr, sizeof(orig_region));
    std::memcpy(orig_timeout, image + BbSfTextLayout::kSignTimeoutVaddr, sizeof(orig_timeout));

    auto restore_sites = [&]() {
        BbSfTextLayout::ResetForTests();
        std::memcpy(image + BbSfTextLayout::kSceneCheck1Vaddr, orig_scene1, sizeof(orig_scene1));
        std::memcpy(image + BbSfTextLayout::kSceneCheck2Vaddr, orig_scene2, sizeof(orig_scene2));
        std::memcpy(image + BbSfTextLayout::kWaterWaveDivVaddr, orig_water, sizeof(orig_water));
        std::memcpy(image + BbSfTextLayout::kMatchingRegionVaddr, orig_region, sizeof(orig_region));
        std::memcpy(image + BbSfTextLayout::kSignTimeoutVaddr, orig_timeout, sizeof(orig_timeout));
    };

    // 1. Default installation enables Scaleform guard, scene both-sides check, and Worldwide matching.
    unsetenv("BB_SF_TEXT_GUARD");
    unsetenv("BB_SCENE_BOTH_SIDES");
    unsetenv("BB_WATER_WAVE_FULL");
    unsetenv("BB_MATCHING_REGION");
    unsetenv("BB_SIGN_TIMEOUT");
    restore_sites();

    BbSfTextLayout::Install(image, kImageSize);
    auto stats = BbSfTextLayout::GetStats();
    assert(stats.guard_installed);
    assert(stats.scene_both_sides_patched);
    assert(!stats.water_wave_full_patched);
    assert(stats.matching_worldwide_patched);
    assert(!stats.sign_timeout_patched);

    const uint8_t expected_scene1[2] = {0x75, 0x11};
    const uint8_t expected_scene2[2] = {0x75, 0x12};
    const uint8_t expected_region[4] = {0xc6, 0x43, 0x0f, 0x01};
    assert(std::memcmp(image + BbSfTextLayout::kSceneCheck1Vaddr, expected_scene1, 2) == 0);
    assert(std::memcmp(image + BbSfTextLayout::kSceneCheck2Vaddr, expected_scene2, 2) == 0);
    assert(std::memcmp(image + BbSfTextLayout::kMatchingRegionVaddr, expected_region, 4) == 0);

    // 2. Test Scaleform TreeCacheText::UpdateTransform null-layout guard + deferred re-queue:
    // Replace orig_update_transform in the trampoline with a ret (0xc3) so we can exercise
    // UpdateTransformForTest / HandleChangesForTest without needing a full Scaleform HAL.
    // Wait: we can test UpdateTransformForTest after temporarily setting the first byte of
    // the trampoline or testing with a synthetic TreeCacheRoot and TreeCacheNode!
    // Even simpler: ResetForTests clears orig_update_transform to nullptr while keeping
    // UpdateTransformForTest and HandleChangesForTest callable!
    BbSfTextLayout::ResetForTests();

    alignas(16) uint8_t fake_root[0x100]{};
    alignas(16) uint8_t fake_node[0x80]{};
    alignas(16) uint8_t fake_node_data[0x100]{};
    void* root_ptr = fake_root;
    std::memcpy(fake_node + BbSfTextLayout::kTreeCacheRootOffset, &root_ptr, sizeof(root_ptr));

    // Layout pointer at +0x98 is nullptr -> should skip layout part and enqueue fake_node into fake_root + 0xa0.
    BbSfTextLayout::UpdateTransformForTest(fake_node, nullptr, fake_node_data, nullptr,
                                           BbSfTextLayout::kUpdateLayoutPart);
    stats = BbSfTextLayout::GetStats();
    assert(stats.layout_null_skips == 1);
    assert(stats.layout_requeued == 1);

    void* queued_head = nullptr;
    std::memcpy(&queued_head, fake_root + BbSfTextLayout::kTreeRootUpdateListOffset, sizeof(queued_head));
    assert(queued_head == fake_node);

    uint32_t update_flags = 0;
    std::memcpy(&update_flags, fake_node + BbSfTextLayout::kTreeCacheUpdateFlagsOffset, sizeof(update_flags));
    assert(update_flags == (BbSfTextLayout::kUpdateInListFlag | BbSfTextLayout::kUpdateLayoutFlag));

    // Non-null layout pointer at +0x98 -> does not skip or re-queue.
    void* non_null_layout = reinterpret_cast<void*>(uintptr_t(0x12345678));
    std::memcpy(fake_node_data + BbSfTextLayout::kNodeDataLayoutOffset, &non_null_layout, sizeof(non_null_layout));
    BbSfTextLayout::UpdateTransformForTest(fake_node, nullptr, fake_node_data, nullptr,
                                           BbSfTextLayout::kUpdateLayoutPart);
    stats = BbSfTextLayout::GetStats();
    assert(stats.layout_null_skips == 1);
    assert(stats.layout_requeued == 1);

    // HandleChanges tracks Change_Text_Layout (0x400).
    BbSfTextLayout::HandleChangesForTest(fake_node, nullptr, BbSfTextLayout::kChangeTextLayout);
    stats = BbSfTextLayout::GetStats();
    assert(stats.layout_change_events == 1);

    // 3. Test opt-in BB_WATER_WAVE_FULL=1, BB_SIGN_TIMEOUT=360, and opt-out toggles.
    restore_sites();
    setenv("BB_SF_TEXT_GUARD", "0", 1);
    setenv("BB_SCENE_BOTH_SIDES", "0", 1);
    setenv("BB_WATER_WAVE_FULL", "1", 1);
    setenv("BB_MATCHING_REGION", "0", 1);
    setenv("BB_SIGN_TIMEOUT", "360", 1);

    BbSfTextLayout::Install(image, kImageSize);
    stats = BbSfTextLayout::GetStats();
    assert(!stats.guard_installed);
    assert(!stats.scene_both_sides_patched);
    assert(stats.water_wave_full_patched);
    assert(!stats.matching_worldwide_patched);
    assert(stats.sign_timeout_patched);
    assert(std::fabs(stats.sign_timeout_seconds - 360.0f) < 1e-3f);

    float patched_timeout = 0.0f;
    std::memcpy(&patched_timeout, image + BbSfTextLayout::kSignTimeoutVaddr, sizeof(patched_timeout));
    assert(std::fabs(patched_timeout - 360.0f) < 1e-3f);

    restore_sites();
    unsetenv("BB_SF_TEXT_GUARD");
    unsetenv("BB_SCENE_BOTH_SIDES");
    unsetenv("BB_WATER_WAVE_FULL");
    unsetenv("BB_MATCHING_REGION");
    unsetenv("BB_SIGN_TIMEOUT");
    return 0;
}
