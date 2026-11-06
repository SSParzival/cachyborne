// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

namespace BbSfTextLayout {

// Retail 1.09 Scaleform::Render::TreeCacheText addresses and layout constants:
//   0x012d5e0  TreeCacheText::UpdateTransform(HAL*, const TreeText::NodeData*,
//                                             const TransformArgs&, unsigned)
//   0x012df80  TreeCacheText::HandleChanges(HAL*, unsigned)
//   0x00dc120  TreeCacheRoot::AddToUpdate(TreeCacheNode*, unsigned)
inline constexpr uintptr_t kUpdateTransformVaddr = 0x12d5e0;
inline constexpr uintptr_t kHandleChangesVaddr = 0x12df80;
inline constexpr uintptr_t kAddToUpdateVaddr = 0xdc120;

inline constexpr uint32_t kUpdateLayoutPart = 0x00000001u;
inline constexpr uint32_t kChangeTextLayout = 0x00000400u;
inline constexpr uint32_t kUpdateLayoutFlag = 0x00010000u;  // TF_NeedUpdateLayout << 16
inline constexpr uint32_t kUpdateInListFlag = 0x80000000u;  // Update_InList

// Retail 1.09 Scaleform struct offsets verified against eboot.bin disassembly:
//   TreeText::NodeData + 0x98 : Ptr<Text::Layout> pLayout
//   TreeCacheNode      + 0x18 : TreeCacheRoot*    pRoot
//   TreeCacheNode      + 0x20 : TreeNode*         pNode
//   TreeCacheNode      + 0x44 : uint32_t          UpdateFlags
//   TreeCacheNode      + 0x48 : TreeCacheNode*    pNextUpdate
//   TreeCacheRoot      + 0xa0 : TreeCacheNode*    pUpdateList
//   TreeCacheRoot      + 0xaa : uint8_t           DepthUpdatesChained
inline constexpr size_t kNodeDataLayoutOffset = 0x98;
inline constexpr size_t kTreeCacheRootOffset = 0x18;
inline constexpr size_t kTreeCacheNodePtrOffset = 0x20;
inline constexpr size_t kTreeCacheUpdateFlagsOffset = 0x44;
inline constexpr size_t kTreeCacheNextUpdateOffset = 0x48;
inline constexpr size_t kTreeRootUpdateListOffset = 0xa0;
inline constexpr size_t kTreeRootDepthUpdatesChainedOffset = 0xaa;

// Retail 1.09 GXSceneContext::Initialize display-buffer size checks and water wave:
inline constexpr uintptr_t kSceneCheck1Vaddr = 0x22c3af3;
inline constexpr uintptr_t kSceneCheck2Vaddr = 0x22c3b44;
inline constexpr uintptr_t kWaterWaveDivVaddr = 0x22c4f5a;

// Retail 1.09 default network matching region (0 = Local, 1 = Worldwide) and SOS sign timeout:
inline constexpr uintptr_t kMatchingRegionVaddr = 0x1c8af63;
inline constexpr uintptr_t kSignTimeoutVaddr = 0x4927a5c;

struct Stats {
    uint64_t layout_null_skips;
    uint64_t layout_requeued;
    uint64_t layout_change_events;
    bool guard_installed;
    bool scene_both_sides_patched;
    bool water_wave_full_patched;
    bool matching_worldwide_patched;
    bool sign_timeout_patched;
    float sign_timeout_seconds;
};

void Install(uint8_t* image, size_t size);
void ResetForTests();
Stats GetStats();

// Exposed for deterministic unit testing of the Scaleform null-layout guard.
void UpdateTransformForTest(void* self, void* hal, const void* node_data,
                            const void* transform_args, uint32_t flags);
void HandleChangesForTest(void* self, void* hal, uint32_t change_bits);

}  // namespace BbSfTextLayout
