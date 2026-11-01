// SPDX-License-Identifier: GPL-2.0-or-later
// Production planning, Vulkan transfers and row writeback; distinct data in
// every mip/slice.
#include "render/runtime.h"
#include "render/scheduler.h"
#include "textures/image.h"
#include "textures/image_download.h"
#include "vulkan/instance.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace VideoCore;

void Exercise(Vulkan::Instance& instance, Vulkan::Scheduler& scheduler, Vulkan::Runtime& runtime,
              vk::Format format, u32 bits, bool compressed, bool depth, bool stencil,
              bool volume = false, bool reduced = false) {
    ImageInfo info;
    info.size = {23, 13, volume ? 5u : 1u};
    info.pitch = compressed ? 28 : 29;
    info.resources = {stencil ? 1u : 5u, volume ? 1u : 3u};
    info.type = volume ? Gcn::ImageType::Color3D : Gcn::ImageType::Color2DArray;
    info.props.is_volume = volume;
    info.props.is_block = compressed;
    info.props.is_depth = depth;
    info.props.has_stencil = stencil;
    info.pixel_format = format;
    info.num_bits = bits;
    info.UpdateSize();
    std::vector<u8> guest(info.guest_size + 32, 0xCD);
    info.guest_address = reinterpret_cast<uintptr_t>(guest.data());
    info.stencil_size = info.pitch * info.size.height;
    std::vector<u8> stencil_guest(u64(info.stencil_size) * info.resources.layers + 32, 0xCD);
    if (stencil)
        info.stencil_addr = reinterpret_cast<uintptr_t>(stencil_guest.data());
    if (reduced)
        info = info.Reduced(2);
    Common::SlotVector<ImageView> views;
    Image image(instance, runtime, views, info);
    for (auto aspect : {depth ? vk::ImageAspectFlagBits::eDepth : vk::ImageAspectFlagBits::eColor,
                        vk::ImageAspectFlagBits::eStencil}) {
        if (aspect == vk::ImageAspectFlagBits::eStencil && !stencil)
            continue;
        const auto planned = PlanImageDownload(info, aspect);
        assert(planned && !planned->slices.empty());
        const auto& plan = *planned;
        auto upload = runtime.GetStagingPool().Request(plan.staging_size, MemoryType::HostUncached);
        std::memset(upload.mapped, 0xAD, plan.staging_size);
        std::vector<u8> expected =
            aspect == vk::ImageAspectFlagBits::eStencil ? stencil_guest : guest;
        for (u32 i = 0; i < plan.slices.size(); ++i) {
            const auto& slice = plan.slices[i];
            for (u32 row = 0; row < slice.rows; ++row) {
                auto* data = upload.mapped + slice.packed.bufferOffset + u64(row) * slice.row_bytes;
                for (u32 b = 0; b < slice.row_bytes; ++b)
                    data[b] = u8(7 + 31 * i + 13 * row + b);
                if (aspect == vk::ImageAspectFlagBits::eDepth && bits == 32) {
                    const float value = float(1 + (i + row) % 7) / 8.0f;
                    for (u32 b = 0; b < slice.row_bytes; b += sizeof(value)) {
                        std::memcpy(data + b, &value, sizeof(value));
                    }
                }
                std::memcpy(expected.data() + slice.guest_offset + u64(row) * slice.row_pitch, data,
                            slice.row_bytes);
            }
        }
        upload.Flush();
        runtime.UploadImage(&image, upload.buffer, plan.Copies(upload.offset, false));
        auto download = runtime.GetStagingPool().Request(plan.staging_size, MemoryType::HostCached);
        std::memset(download.mapped, 0xEF, plan.staging_size);
        download.Flush();
        runtime.DownloadImage(&image, download.buffer, plan.Copies(download.offset, false));
        // Also exercise the direct GPU-to-guest layout, including all padding
        // sentinels.
        auto direct =
            runtime.GetStagingPool().Request(plan.guest_size + 32, MemoryType::HostCached);
        std::memset(direct.mapped, 0xCD, plan.guest_size + 32);
        direct.Flush();
        // Stencil planes with odd slice strides cannot be copied directly (offset %
        // 4).
        const bool aligned =
            std::ranges::all_of(plan.slices, [](const auto& s) { return s.guest_offset % 4 == 0; });
        if (aligned)
            runtime.DownloadImage(&image, direct.buffer, plan.Copies(direct.offset, true));
        scheduler.Finish();
        download.Invalidate();
        plan.WriteRows(download.mapped,
                       [](u64 address, const u8* data, u64 bytes, u64 pitch, u64 rows) {
                           for (u64 row = 0; row < rows; ++row) {
                               std::memcpy(reinterpret_cast<void*>(address + row * pitch),
                                           data + row * bytes, bytes);
                           }
                       });
        const auto& result = aspect == vk::ImageAspectFlagBits::eStencil ? stencil_guest : guest;
        if (result != expected) {
            for (size_t at = 0; at < result.size(); ++at) {
                if (result[at] != expected[at]) {
                    std::fprintf(stderr,
                                 "format=%u aspect=%u volume=%d reduced=%d byte=%zu "
                                 "actual=%u expected=%u\n",
                                 u32(format), u32(aspect), volume, reduced, at, result[at],
                                 expected[at]);
                    break;
                }
            }
            std::abort();
        } // Includes untouched row/slice/mip padding and trailing guard.
        if (aligned) {
            direct.Invalidate();
            assert(std::memcmp(direct.mapped, expected.data(), expected.size()) == 0);
        }
        assert(plan.staging_size <= plan.guest_size + 16 * plan.slices.size());
    }
    std::printf("download passed: format=%u volume=%d reduced=%d\n", u32(format), volume, reduced);
}

int main() {
    Vulkan::Instance instance(0, false);
    Vulkan::Scheduler scheduler(instance);
    Vulkan::Runtime runtime(instance, scheduler);
    Exercise(instance, scheduler, runtime, vk::Format::eR8G8B8A8Unorm, 32, false, false, false);
    Exercise(instance, scheduler, runtime, vk::Format::eR8G8B8A8Unorm, 32, false, false, false,
             true);
    Exercise(instance, scheduler, runtime, vk::Format::eR8G8B8A8Unorm, 32, false, false, false,
             false, true);
    Exercise(instance, scheduler, runtime, vk::Format::eBc1RgbaUnormBlock, 64, true, false, false);
    Exercise(instance, scheduler, runtime, vk::Format::eBc3UnormBlock, 128, true, false, false);
    Exercise(instance, scheduler, runtime, vk::Format::eBc3UnormBlock, 128, true, false, false, false, true);
    Exercise(instance, scheduler, runtime, vk::Format::eBc6HUfloatBlock, 128, true, false, false);
    Exercise(instance, scheduler, runtime, vk::Format::eD16Unorm, 16, false, true, false);
    Exercise(instance, scheduler, runtime, vk::Format::eD32Sfloat, 32, false, true, false);
    Exercise(instance, scheduler, runtime, vk::Format::eD32SfloatS8Uint, 32, false, true, true);
    ImageInfo invalid;
    assert(!PlanImageDownload(invalid, vk::ImageAspectFlagBits::eColor));
    invalid.pixel_format = vk::Format::eR8G8B8A8Unorm;
    invalid.num_bits = 32;
    invalid.pitch = 16;
    invalid.size = {16, 16, 1};
    invalid.resources = {3, 1};
    invalid.type = Gcn::ImageType::Color2D;
    invalid.UpdateSize();
    const auto complete = PlanImageDownload(invalid, vk::ImageAspectFlagBits::eColor);
    assert(complete && complete->slices.size() == 3);
    invalid.guest_size = 4;
    assert(!PlanImageDownload(invalid, vk::ImageAspectFlagBits::eColor));
    invalid.UpdateSize();
    invalid.num_samples = 4;
    assert(!PlanImageDownload(invalid, vk::ImageAspectFlagBits::eColor));
    invalid.num_samples = 1;
    invalid.tile_mode = Gcn::TileMode::Display2DThin;
    assert(!PlanImageDownload(invalid, vk::ImageAspectFlagBits::eColor));
    invalid.tile_mode = Gcn::TileMode::DisplayLinearAligned;
    invalid.guest_address = ~u64{0} - 2;
    assert(!PlanImageDownload(invalid, vk::ImageAspectFlagBits::eColor));
    invalid.resources = {17, 1};
    assert(!PlanImageDownload(invalid, vk::ImageAspectFlagBits::eColor));
    vk::BufferImageCopy bc{.imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 2},
                           .imageExtent = {7, 5, 1}};
    assert(ImageCopyByteSize(bc, vk::Format::eBc1RgbaUnormBlock) == 64);
    bc.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eStencil;
    assert(ImageCopyByteSize(bc, vk::Format::eD32SfloatS8Uint) == 70);
}
