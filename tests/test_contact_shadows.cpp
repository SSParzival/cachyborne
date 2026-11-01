// SPDX-License-Identifier: GPL-2.0-or-later
// Actual production SPIR-V on Vulkan: flat receivers, fully lit/occluded PCSS,
// atlas edge isolation, non-multiple workgroups and unchanged RGB channels.
#include "bbgpu/shaders/contact_shadow_comp.h"
#include "render/scheduler.h"
#include "render/vk_contact_shadow.h"
#include "test_vk_image.h"
#include "vulkan/instance.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vk_mem_alloc.h>

int main() {
    Vulkan::Instance instance(0, false);
    Vulkan::Scheduler scheduler(instance);
    const auto device = instance.GetDevice();
    static vk::detail::DynamicLoader loader;
    vk::detail::DispatchLoaderDynamic d;
    d.init(loader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr"));
    d.init(instance.GetInstance());
    d.init(device);
    constexpr u32 W = 33, H = 19, N = W * H, Atlas = 16;
    std::array<TestImage, 3> images;
    std::array<vk::UniqueImageView, 3> views;
    for (u32 i = 0; i < 3; ++i) {
        const auto format = i == 1 ? vk::Format::eR8G8B8A8Unorm : vk::Format::eR32Sfloat;
        images[i] = TestImage(device, instance.GetAllocator());
        images[i].Create(
            {.imageType = vk::ImageType::e2D,
             .format = format,
             .extent = {i == 2 ? Atlas : W, i == 2 ? Atlas : H, 1},
             .mipLevels = 1,
             .arrayLayers = 1,
             .samples = vk::SampleCountFlagBits::e1,
             .tiling = vk::ImageTiling::eOptimal,
             .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage |
                      vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst});
        views[i] = device
                       .createImageViewUnique(
                           {.image = vk::Image(images[i]),
                            .viewType = vk::ImageViewType::e2D,
                            .format = format,
                            .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}},
                           nullptr, d)
                       .value;
    }
    constexpr std::size_t Depth = 1024, Shadow = Depth + N * 4, Output = Shadow + Atlas * Atlas * 4,
                          Bytes = Output + N * 4;
    VkBuffer buffer{};
    VmaAllocation allocation{};
    VmaAllocationInfo ai{};
    const VkBufferCreateInfo bi{.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                .size = Bytes,
                                .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                                         VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT};
    const VmaAllocationCreateInfo ac{.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT |
                                              VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
                                     .usage = VMA_MEMORY_USAGE_AUTO,
                                     .requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};
    assert(vmaCreateBuffer(instance.GetAllocator(), &bi, &ac, &buffer, &allocation, &ai) ==
           VK_SUCCESS);
    auto *floats = static_cast<float *>(ai.pMappedData);
    std::memset(floats, 0, Bytes);
    for (u32 matrix = 0; matrix < 5; ++matrix)
        for (u32 axis = 0; axis < 4; ++axis)
            floats[matrix * 16 + axis * 5] = 1;
    floats[80] = 10;
    floats[81] = 20;
    floats[82] = 30;
    floats[128 + 72] = 100;
    floats[128 + 73] = 1;
    for (u32 p = 0; p < N; ++p)
        floats[Depth / 4 + p] = .5f;
    const std::array<vk::DescriptorType, 5> types{
        vk::DescriptorType::eStorageBuffer, vk::DescriptorType::eCombinedImageSampler,
        vk::DescriptorType::eStorageImage, vk::DescriptorType::eCombinedImageSampler,
        vk::DescriptorType::eStorageBuffer};
    std::array<vk::DescriptorSetLayoutBinding, 5> bindings;
    for (u32 i = 0; i < 5; ++i)
        bindings[i] = {.binding = i,
                       .descriptorType = types[i],
                       .descriptorCount = 1,
                       .stageFlags = vk::ShaderStageFlagBits::eCompute};
    auto desc = device
                    .createDescriptorSetLayoutUnique(
                        {.flags = vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR,
                         .bindingCount = 5,
                         .pBindings = bindings.data()},
                        nullptr, d)
                    .value;
    struct Push {
        u32 off = 0, frame = 0;
        std::array<float, 2> size{W, H};
        float length = .6f, thickness = .35f, strength = .75f, flip = 1;
        u32 debug = 0, contact = 0, pcss = 0;
        float tangent = .02f, max_penumbra = .3f, min_penumbra = 0, bias = .04f;
        u32 filter = 0;
        float blend = .1f;
    };
    static_assert(sizeof(Push) == 68);
    const vk::PushConstantRange range{vk::ShaderStageFlagBits::eCompute, 0, sizeof(Push)};
    auto layout = device
                      .createPipelineLayoutUnique({.setLayoutCount = 1,
                                                   .pSetLayouts = &*desc,
                                                   .pushConstantRangeCount = 1,
                                                   .pPushConstantRanges = &range},
                                                  nullptr, d)
                      .value;
    auto module =
        device
            .createShaderModuleUnique(
                {.codeSize = sizeof(CONTACT_SHADOW_COMP), .pCode = CONTACT_SHADOW_COMP}, nullptr, d)
            .value;
    auto pipeline =
        device
            .createComputePipelineUnique({},
                                         {.stage = {.stage = vk::ShaderStageFlagBits::eCompute,
                                                    .module = *module,
                                                    .pName = "main"},
                                          .layout = *layout},
                                         nullptr, d)
            .value;
    auto sampler = device
                       .createSamplerUnique({.magFilter = vk::Filter::eNearest,
                                             .minFilter = vk::Filter::eNearest,
                                             .addressModeU = vk::SamplerAddressMode::eClampToEdge,
                                             .addressModeV = vk::SamplerAddressMode::eClampToEdge},
                                            nullptr, d)
                       .value;
    const auto barrier = [&](vk::CommandBuffer cmd, vk::PipelineStageFlags2 from,
                             vk::AccessFlags2 access, vk::PipelineStageFlags2 to,
                             vk::AccessFlags2 after) {
        const vk::MemoryBarrier2 b{.srcStageMask = from,
                                   .srcAccessMask = access,
                                   .dstStageMask = to,
                                   .dstAccessMask = after};
        cmd.pipelineBarrier2({.memoryBarrierCount = 1, .pMemoryBarriers = &b}, d);
    };
    for (u32 mode = 0; mode < 8; ++mode) {
        for (u32 y = 0; y < Atlas; ++y)
            for (u32 x = 0; x < Atlas; ++x)
                floats[Shadow / 4 + y * Atlas + x] =
                    (mode >= 2 && x < Atlas / 2 && y < Atlas / 2) ? 0 : 1;
        const auto cmd = scheduler.CommandBuffer();
        std::array<vk::ImageMemoryBarrier2, 3> transitions;
        for (u32 i = 0; i < 3; ++i)
            transitions[i] = {.srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
                              .dstStageMask = vk::PipelineStageFlagBits2::eAllCommands,
                              .dstAccessMask = vk::AccessFlagBits2::eMemoryWrite |
                                               vk::AccessFlagBits2::eMemoryRead,
                              .oldLayout = vk::ImageLayout::eUndefined,
                              .newLayout = vk::ImageLayout::eGeneral,
                              .image = vk::Image(images[i]),
                              .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
        cmd.pipelineBarrier2(
            {.imageMemoryBarrierCount = 3, .pImageMemoryBarriers = transitions.data()}, d);
        for (u32 i : {0u, 2u}) {
            const vk::BufferImageCopy copy{
                .bufferOffset = i == 0 ? Depth : Shadow,
                .imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                .imageExtent = {i == 0 ? W : Atlas, i == 0 ? H : Atlas, 1}};
            cmd.copyBufferToImage(buffer, vk::Image(images[i]), vk::ImageLayout::eGeneral, copy, d);
        }
        cmd.clearColorImage(vk::Image(images[1]), vk::ImageLayout::eGeneral,
                            vk::ClearColorValue{std::array<float, 4>{.2f, .4f, .6f, 1}},
                            vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
                            d);
        barrier(cmd, vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite,
                vk::PipelineStageFlagBits2::eComputeShader,
                vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite);
        const vk::DescriptorBufferInfo cb{buffer, 0, 512}, ub{buffer, 512, 512};
        const vk::DescriptorImageInfo depth{*sampler, *views[0], vk::ImageLayout::eGeneral},
            target{{}, *views[1], vk::ImageLayout::eGeneral};
        // Match the guest renderer: image_infos has a view but no sampler.
        Vulkan::ContactShadow::Inputs input{
            .constants = cb,
            .uniforms = ub,
            .shadow_map = {{}, *views[2], vk::ImageLayout::eGeneral}};
        if (mode == 4)
            input.shadow_map.imageView = nullptr;
        if (mode == 5)
            input.uniforms.buffer = nullptr;
        if (mode == 6)
            input.uniforms.range = 73 * 4; // needs 74 dwords
        if (mode == 7)
            input.shadow_map.imageLayout = vk::ImageLayout::eUndefined;
        bool filter = mode != 0;
        const auto safe = Vulkan::ContactShadow::SafeInputs(input, depth, filter);
        assert(filter == (mode >= 1 && mode <= 3));
        assert(safe.uniforms.buffer && safe.shadow_map.imageView && safe.shadow_map.sampler);
        assert(safe.shadow_map.sampler == *sampler);
        if (!filter) {
            assert(safe.uniforms.buffer == cb.buffer && safe.uniforms.offset == cb.offset);
            assert(safe.shadow_map.imageView == depth.imageView);
        }
        const auto &atlas = safe.shadow_map;
        const auto &uniforms = safe.uniforms;
        const std::array<vk::WriteDescriptorSet, 5> writes{{{.dstBinding = 0,
                                                             .descriptorCount = 1,
                                                             .descriptorType = types[0],
                                                             .pBufferInfo = &cb},
                                                            {.dstBinding = 1,
                                                             .descriptorCount = 1,
                                                             .descriptorType = types[1],
                                                             .pImageInfo = &depth},
                                                            {.dstBinding = 2,
                                                             .descriptorCount = 1,
                                                             .descriptorType = types[2],
                                                             .pImageInfo = &target},
                                                            {.dstBinding = 3,
                                                             .descriptorCount = 1,
                                                             .descriptorType = types[3],
                                                             .pImageInfo = &atlas},
                                                            {.dstBinding = 4,
                                                             .descriptorCount = 1,
                                                             .descriptorType = types[4],
                                                             .pBufferInfo = &uniforms}}};
        Push push;
        push.contact = mode == 0 || mode >= 3;
        push.pcss = push.filter = filter;
        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline, d);
        cmd.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute, *layout, 0, writes, d);
        cmd.pushConstants(*layout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(push), &push, d);
        cmd.dispatch((W + 7) / 8, (H + 7) / 8, 1, d);
        barrier(cmd, vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderWrite,
                vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferRead);
        const vk::BufferImageCopy copy{
            .bufferOffset = Output,
            .imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
            .imageExtent = {W, H, 1}};
        cmd.copyImageToBuffer(vk::Image(images[1]), vk::ImageLayout::eGeneral, buffer, copy, d);
        barrier(cmd, vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite,
                vk::PipelineStageFlagBits2::eHost, vk::AccessFlagBits2::eHostRead);
        scheduler.Finish();
        const auto *pixels = static_cast<unsigned char *>(ai.pMappedData) + Output;
        for (u32 p = 0; p < N; ++p)
            assert(pixels[p * 4] == 51 && pixels[p * 4 + 1] == 102 && pixels[p * 4 + 2] == 153);
        // Receiver near cascade 0's atlas edge: adjacent cascades are lit.
        const auto alpha = pixels[(6 * W + 24) * 4 + 3];
        assert((mode == 2 || mode == 3) ? alpha == 0 : alpha == 255);
        assert(pixels[(18 * W) * 4 + 3] == 255); // outside cascade: native shadow retained
        std::printf("PASS: contact/PCSS mode %u, edge alpha %u, RGB and bounds intact\n", mode,
                    alpha);
    }
    vmaDestroyBuffer(instance.GetAllocator(), buffer, allocation);
}
