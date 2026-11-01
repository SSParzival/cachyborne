// SPDX-License-Identifier: GPL-2.0-or-later
#include "textures/image_download.h"

#include <algorithm>
#include <numeric>
#include <vulkan/vulkan_format_traits.hpp>

namespace VideoCore {
u32 ImageCopyBlockBytes(vk::Format format, vk::ImageAspectFlags aspect) {
    if (aspect == vk::ImageAspectFlagBits::eStencil) {
        return 1;
    }
    if (aspect == vk::ImageAspectFlagBits::eDepth) {
        return format == vk::Format::eD16Unorm || format == vk::Format::eD16UnormS8Uint ? 2 : 4;
    }
    return vk::blockSize(format);
}

u64 ImageCopyByteSize(const vk::BufferImageCopy& copy, vk::Format format) {
    const auto block = vk::blockExtent(format);
    const u64 bytes = ImageCopyBlockBytes(format, copy.imageSubresource.aspectMask);
    const auto ceil = [](u64 n, u32 d) { return (n + d - 1) / d; };
    const u64 row =
        ceil(copy.bufferRowLength ? copy.bufferRowLength : copy.imageExtent.width, block[0]) *
        bytes;
    const u64 slice =
        ceil(copy.bufferImageHeight ? copy.bufferImageHeight : copy.imageExtent.height, block[1]) *
        row;
    const u64 slices = u64(copy.imageExtent.depth) * copy.imageSubresource.layerCount;
    return (slices - 1) * slice + (ceil(copy.imageExtent.height, block[1]) - 1) * row +
           ceil(copy.imageExtent.width, block[0]) * bytes;
}

std::optional<ImageDownloadPlan> PlanImageDownload(const ImageInfo& info,
                                                   vk::ImageAspectFlagBits aspect) {
    const bool stencil = aspect == vk::ImageAspectFlagBits::eStencil;
    if (info.pixel_format == vk::Format::eUndefined ||
        (aspect != vk::ImageAspectFlagBits::eColor && aspect != vk::ImageAspectFlagBits::eDepth &&
         aspect != vk::ImageAspectFlagBits::eStencil) ||
        (info.props.is_depth != (aspect != vk::ImageAspectFlagBits::eColor)) || info.IsTiled() ||
        info.num_samples != 1 || !info.resources.levels ||
        info.resources.levels > info.mips_layout.size() || !info.resources.layers ||
        !info.size.width || !info.size.height || !info.size.depth ||
        (stencil && (!info.props.has_stencil || info.resources.levels != 1 || !info.stencil_addr ||
                     !info.stencil_size))) {
        return std::nullopt;
    }
    const auto block = vk::blockExtent(info.pixel_format);
    const u32 bytes = ImageCopyBlockBytes(info.pixel_format, aspect);
    ImageDownloadPlan plan;
    plan.guest_address = stencil ? info.stencil_addr : info.guest_address;
    plan.guest_size = stencil ? u64(info.stencil_size) * info.resources.layers : info.guest_size;
    if (!bytes || !plan.guest_size || plan.guest_address > ~u64{} - plan.guest_size) {
        return std::nullopt;
    }
    for (u32 mip = 0; mip < info.resources.levels; ++mip) {
        const auto& layout = info.mips_layout[mip];
        const u32 width = std::max(info.size.width >> mip, 1u);
        const u32 height = std::max(info.size.height >> mip, 1u);
        const bool volume = info.type == Gcn::ImageType::Color3D;
        const u32 depth = volume ? std::max(info.size.depth >> mip, 1u) : 1;
        if (volume && info.resources.layers != 1) {
            return std::nullopt;
        }
        const u64 slices = u64(depth) * info.resources.layers;
        if (!stencil && layout.size % slices != 0) {
            return std::nullopt;
        }
        const u64 stride = stencil ? info.stencil_size : layout.size / slices;
        const u32 pitch = stencil ? info.pitch : layout.pitch;
        const u32 rows = (height + block[1] - 1) / block[1];
        const u64 row_pitch = u64(pitch / block[0]) * bytes;
        const u64 row_bytes = u64((width + block[0] - 1) / block[0]) * bytes;
        if (pitch < width || pitch % block[0] || row_pitch > UINT32_MAX || row_bytes > UINT32_MAX ||
            (rows - 1) * row_pitch + row_bytes > stride) {
            return std::nullopt;
        }
        for (u32 slice = 0; slice < slices; ++slice) {
            const u64 guest_offset = (stencil ? 0 : layout.offset) + slice * stride;
            const u64 end = guest_offset + (rows - 1) * row_pitch + row_bytes;
            if (end > plan.guest_size) {
                return std::nullopt;
            }
            const u64 alignment = std::lcm<u64>(4, bytes);
            plan.staging_size = (plan.staging_size + alignment - 1) / alignment * alignment;
            plan.slices.push_back({
                .packed{
                    .bufferOffset = plan.staging_size,
                    .imageSubresource = {aspect, mip, volume ? 0u : slice, 1},
                    .imageOffset = {0, 0, volume ? s32(slice) : 0},
                    .imageExtent = {width, height, 1},
                },
                .guest_offset = guest_offset,
                .row_bytes = u32(row_bytes),
                .row_pitch = u32(row_pitch),
                .row_length = pitch,
                .rows = rows,
            });
            plan.staging_size += row_bytes * rows;
        }
    }
    return plan;
}

boost::container::small_vector<vk::BufferImageCopy, 16>
ImageDownloadPlan::Copies(u64 base, bool guest_layout) const {
    boost::container::small_vector<vk::BufferImageCopy, 16> copies;
    copies.reserve(slices.size());
    for (const auto& slice : slices) {
        auto copy = slice.packed;
        copy.bufferOffset = base + (guest_layout ? slice.guest_offset : copy.bufferOffset);
        if (guest_layout) {
            // A separate region per slice preserves padding between layers/depth
            // slices.
            copy.bufferRowLength = slice.row_length;
        }
        copies.push_back(copy);
    }
    return copies;
}
} // namespace VideoCore
