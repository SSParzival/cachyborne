// SPDX-License-Identifier: GPL-2.0-or-later
// Exercise real Vulkan clears/copies and their production guest-memory ownership hook.
#include <cassert>
#include <cstring>
#include <vector>
#include <xxhash.h>
#include "vulkan/instance.h"
#include "render/scheduler.h"
#include "render/runtime.h"
#include "textures/image.h"

extern "C" void runtime_test_set_backing_reader(void (*)(uintptr_t, void*, uint64_t));
namespace {
std::vector<u8> source, target, stencil;
void Read(uintptr_t address, void* out, uint64_t size) {
    for (const auto* bytes : {&source, &target, &stencil}) {
        const auto begin = reinterpret_cast<uintptr_t>(bytes->data());
        if (address >= begin && address - begin <= bytes->size() &&
            size <= bytes->size() - (address - begin)) {
            std::memcpy(out, bytes->data() + address - begin, size);
            return;
        }
    }
    std::abort();
}
}
int main() {
    Vulkan::Instance instance(0, false);
    Vulkan::Scheduler scheduler(instance);
    Vulkan::Runtime runtime(instance, scheduler);
    Common::SlotVector<VideoCore::ImageView> views;
    VideoCore::ImageInfo info;
    info.size = {16, 16, 1};
    info.pitch = 16;
    info.resources = {1, 1};
    info.type = Gcn::ImageType::Color2D;
    info.pixel_format = vk::Format::eR8G8B8A8Unorm;
    info.num_bits = 32;
    info.UpdateSize();
    source.resize(info.guest_size, 0x17);
    target.resize(info.guest_size, 0x31);
    runtime_test_set_backing_reader(Read);
    info.guest_address = reinterpret_cast<uintptr_t>(source.data());
    VideoCore::Image src(instance, runtime, views, info);
    info.guest_address = reinterpret_cast<uintptr_t>(target.data());
    VideoCore::Image dst(instance, runtime, views, info);
    vk::ClearValue clear{};
    clear.color.float32 = std::array{0.2f, 0.3f, 0.4f, 1.0f};
    runtime.ClearImage(&src, {}, clear);
    assert(src.gpu_base_valid && src.gpu_base_hash == XXH3_64bits(source.data(), source.size()));
    runtime.CopyImage(&src, &dst);
    assert(dst.gpu_base_valid && dst.gpu_base_hash == XXH3_64bits(target.data(), target.size()));
    const auto original = dst.gpu_base_hash;
    target.back() ^= 0x80;
    runtime.ClearImage(&dst, {}, clear);
    assert(dst.gpu_base_hash == original); // A second GPU write must retain the first baseline.
    assert(dst.GuestMemoryHash() != original); // Reused guest memory is detectable.
    dst.gpu_base_valid = false; // A subsequent upload starts a fresh ownership interval.
    runtime.ClearImage(&dst, {}, clear);
    assert(dst.gpu_base_hash == XXH3_64bits(target.data(), target.size()));
    // Depth/stencil uses distinct guest allocations: either allocation changing vetoes GC.
    info.pixel_format = vk::Format::eD32SfloatS8Uint;
    info.props.is_depth = true;
    info.props.has_stencil = true;
    stencil.resize(info.pitch * info.size.height, 0x42);
    info.stencil_addr = reinterpret_cast<uintptr_t>(stencil.data());
    info.stencil_size = stencil.size();
    VideoCore::Image depth(instance, runtime, views, info);
    clear.depthStencil = vk::ClearDepthStencilValue{0.25f, 7};
    runtime.ClearImage(&depth, {}, clear);
    assert(depth.gpu_base_valid);
    const auto depth_base = depth.gpu_base_hash;
    stencil.back() ^= 0x80;
    assert(depth.GuestMemoryHash() != depth_base);
    depth.UpdateStencilMemory(info.stencil_addr, info.stencil_size);
    assert(depth.gpu_base_hash == depth_base && depth.gpu_base_valid);
    depth.UpdateStencilMemory(0, 0);
    assert(!depth.gpu_base_valid && depth.info.stencil_addr == 0);
    depth.UpdateStencilMemory(info.stencil_addr, info.stencil_size);
    assert(!depth.gpu_base_valid);
    runtime.ClearImage(&depth, {}, clear);
    assert(depth.gpu_base_valid && depth.gpu_base_hash == depth.GuestMemoryHash());
    scheduler.Finish();
}
