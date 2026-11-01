// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "textures/image_info.h"
#include <boost/container/small_vector.hpp>
#include <optional>

namespace VideoCore {
/// A slice of a mip. Staging is tightly packed; only its valid rows reach guest
/// memory.
struct ImageDownloadSlice {
    vk::BufferImageCopy packed;
    u64 guest_offset;
    u32 row_bytes;
    u32 row_pitch;
    u32 row_length;
    u32 rows;
};
struct ImageDownloadPlan {
    u64 guest_address{};
    u64 guest_size{};
    u64 staging_size{};
    boost::container::small_vector<ImageDownloadSlice, 16> slices;

    boost::container::small_vector<vk::BufferImageCopy, 16> Copies(u64 base,
                                                                   bool guest_layout) const;
    /// One runtime transaction per slice; the writer never receives guest padding as data.
    template <typename Write> void WriteRows(const u8* mapped, Write&& write) const {
        for (const auto& slice : slices) {
            write(guest_address + slice.guest_offset, mapped + slice.packed.bufferOffset,
                  slice.row_bytes, slice.row_pitch, slice.rows);
        }
    }
};
/// Reject layouts we cannot safely restore (tiling/MSAA/invalid bounds).
/// Reduced images keep their original guest mip offsets. Stencil targets have a
/// separate one-byte plane.
std::optional<ImageDownloadPlan> PlanImageDownload(const ImageInfo& info,
                                                   vk::ImageAspectFlagBits aspect);
/// Vulkan copies depth/stencil aspects separately, with their aspect's texel
/// size.
u32 ImageCopyBlockBytes(vk::Format format, vk::ImageAspectFlags aspect);
u64 ImageCopyByteSize(const vk::BufferImageCopy& copy, vk::Format format);
} // namespace VideoCore
