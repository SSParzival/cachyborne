// SPDX-License-Identifier: GPL-2.0-or-later
// Host integration written for Cachyborne. Shader from Lantern Engine by Awaken,
// e6b943dd; modified here with explicit buffer/layout/lifetime validation.
#include "vk_contact_shadow.h"
#include "bbgpu/shaders/contact_shadow_comp.h"
#include "render/runtime.h"
#include "render/scheduler.h"
#include "render/vk_gpu_profiler.h"
#include "shim/bb_env.h"
#include "shim/bbport_settings.h"
#include "textures/texture_cache.h"
#include "upscale/vk_camera_motion.h"
#include "upscale/vk_scene_resolution.h"
#include "vulkan/instance.h"
#include "vulkan/shader_module.h"
#include <array>
#include <cstdio>

namespace Vulkan {
namespace {
constexpr auto Compute = vk::PipelineStageFlagBits2::eComputeShader;
constexpr auto Read = vk::AccessFlagBits2::eShaderRead;
constexpr auto Write = vk::AccessFlagBits2::eShaderWrite;
struct Push {
    u32 offsets, frame;
    std::array<float, 2> size;
    float length = .6f, thickness = .35f, strength = .75f, flip = 1;
    u32 debug = 0, contact = 0, pcss = 0;
    float tangent = .02f, max_penumbra = .3f, min_penumbra = 0, bias = .04f;
    u32 filter = 0;
    float cascade_blend = .1f;
};
static_assert(sizeof(Push) == 68);
bool Range(const vk::DescriptorBufferInfo &buffer, u32 offset, u32 words) {
    return buffer.buffer && offset % 4 == 0 && buffer.range != VK_WHOLE_SIZE &&
           u64(offset) + u64(words) * 4 <= buffer.range;
}
bool Contact() {
    const char *override = bb_env("BB_CONTACT_SHADOWS");
    return override ? override[0] == '1' : BbSettings::Get().contact_shadows.load();
}
bool Pcss() {
    const char *override = bb_env("BB_PCSS");
    return override ? override[0] == '1' : BbSettings::Get().pcss.load();
}
} // namespace
bool ContactShadow::Wanted() { return Contact() || Pcss(); }
ContactShadow::Inputs ContactShadow::SafeInputs(const Inputs &source,
                                                const vk::DescriptorImageInfo &depth,
                                                bool &filter) {
    Inputs result = source;
    filter = filter && Range(source.uniforms, (source.offsets >> 8) & 255, 74) &&
             source.shadow_map.imageView &&
             source.shadow_map.imageLayout != vk::ImageLayout::eUndefined;
    if (!filter) {
        result.uniforms = source.constants;
        result.shadow_map = depth;
    }
    // image_infos carries a sampled image, not a combined-image-sampler. Its
    // sampler is normally null even when the atlas view is valid. RADV must
    // never receive that null sampler in our combined descriptor binding.
    result.shadow_map.sampler = depth.sampler;
    return result;
}
ContactShadow::ContactShadow(const Instance &i, Scheduler &s, Runtime &r,
                             VideoCore::TextureCache &t, SceneTargets &sc, CameraMotion &c)
    : instance(i), scheduler(s), runtime(r), textures(t), scenes(sc), camera(c) {
    const auto device = instance.GetDevice();
    const std::array<vk::DescriptorType, 5> types{
        vk::DescriptorType::eStorageBuffer, vk::DescriptorType::eCombinedImageSampler,
        vk::DescriptorType::eStorageImage, vk::DescriptorType::eCombinedImageSampler,
        vk::DescriptorType::eStorageBuffer};
    std::array<vk::DescriptorSetLayoutBinding, 5> bindings;
    for (u32 n = 0; n < bindings.size(); ++n)
        bindings[n] = {.binding = n,
                       .descriptorType = types[n],
                       .descriptorCount = 1,
                       .stageFlags = vk::ShaderStageFlagBits::eCompute};
    descriptors = Check(device.createDescriptorSetLayoutUnique(
        {.flags = vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR,
         .bindingCount = u32(bindings.size()),
         .pBindings = bindings.data()}));
    const vk::PushConstantRange range{vk::ShaderStageFlagBits::eCompute, 0, sizeof(Push)};
    layout = Check(device.createPipelineLayoutUnique({.setLayoutCount = 1,
                                                      .pSetLayouts = &*descriptors,
                                                      .pushConstantRangeCount = 1,
                                                      .pPushConstantRanges = &range}));
    const auto module = CompileSPV(CONTACT_SHADOW_COMP, device);
    auto result = device.createComputePipelineUnique(
        {}, vk::ComputePipelineCreateInfo{.stage = {.stage = vk::ShaderStageFlagBits::eCompute,
                                                    .module = module,
                                                    .pName = "main"},
                                          .layout = *layout});
    device.destroyShaderModule(module);
    pipeline = Check(std::move(result));
    sampler =
        Check(device.createSamplerUnique({.magFilter = vk::Filter::eNearest,
                                          .minFilter = vk::Filter::eNearest,
                                          .mipmapMode = vk::SamplerMipmapMode::eNearest,
                                          .addressModeU = vk::SamplerAddressMode::eClampToEdge,
                                          .addressModeV = vk::SamplerAddressMode::eClampToEdge,
                                          .addressModeW = vk::SamplerAddressMode::eClampToEdge,
                                          .maxLod = 0}));
}
ContactShadow::~ContactShadow() = default;
void ContactShadow::OnResolve(VideoCore::ImageId target, const Inputs &in) {
    pending = {};
    if (!Wanted() || !target || !Range(in.constants, in.offsets & 255, 84))
        return;
    pending = target;
    pending_uid = textures.GetImage(target).image_uid;
    inputs = in;
}
void ContactShadow::OnBlur(VideoCore::ImageId target, VAddr source) {
    if (!pending)
        return;
    const auto &old = textures.GetImage(pending);
    if (!target || old.image_uid != pending_uid || old.info.guest_address != source) {
        pending = {};
        return;
    }
    pending = target;
    pending_uid = textures.GetImage(target).image_uid;
}
void ContactShadow::Flush(bool blur_next) {
    if (!pending || blur_next)
        return;
    const auto target_id = pending;
    pending = {};
    const auto depth_id = camera.Depth();
    if (!depth_id || !Wanted())
        return;
    auto &target = textures.GetImage(target_id);
    auto &depth = textures.GetImage(depth_id);
    if (target.image_uid != pending_uid || target.info.pixel_format != vk::Format::eR8G8B8A8Unorm ||
        !depth.info.props.is_depth || target.info.num_samples != 1 || depth.info.num_samples != 1 ||
        target.info.resources.layers != 1 || depth.info.resources.layers != 1)
        return;
    scheduler.EndRendering();
    u32 width = target.info.size.width, height = target.info.size.height;
    vk::ImageView target_view{}, depth_view{};
    if (scenes.Reduced() && scenes.Eligible(target) && scenes.Eligible(depth)) {
        VideoCore::ImageViewInfo ti, di;
        ti.format = target.info.pixel_format;
        di.format = depth.info.pixel_format;
        const auto out = scenes.Read(target_id, ti, Compute, Read | Write);
        if (!(out.usage & vk::ImageUsageFlagBits::eStorage))
            return;
        target_view = out.view;
        depth_view = scenes.Read(depth_id, di, Compute, Read).view;
        const auto size = scenes.ProxySize(target);
        width = size.width;
        height = size.height;
    } else {
        if (!(target.usage_flags & vk::ImageUsageFlagBits::eStorage))
            return;
        const auto device = instance.GetDevice();
        target_view = Check(device.createImageView(
            {.image = target.GetImage(),
             .viewType = vk::ImageViewType::e2D,
             .format = target.info.pixel_format,
             .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}}));
        depth_view = Check(device.createImageView(
            {.image = depth.GetImage(),
             .viewType = vk::ImageViewType::e2D,
             .format = depth.info.pixel_format,
             .subresourceRange = {vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1}}));
        scheduler.DeferOperation([device, target_view, depth_view] {
            device.destroyImageView(target_view);
            device.destroyImageView(depth_view);
        });
        runtime.Transit(&target, vk::ImageLayout::eGeneral, Compute, Read | Write);
        runtime.Transit(&depth, vk::ImageLayout::eGeneral, Compute, Read);
        runtime.FlushBarriers();
    }
    Push push{.offsets = inputs.offsets, .frame = frame++, .size = {float(width), float(height)}};
    push.contact = Contact();
    if (!target_view || !depth_view || !sampler ||
        !Range(inputs.constants, inputs.offsets & 255, 84))
        return;
    bool filter = Pcss();
    const auto in = SafeInputs(inputs, {*sampler, depth_view, vk::ImageLayout::eGeneral}, filter);
    push.pcss = filter;
    push.filter = filter;
    if (!push.contact && !push.pcss)
        return; // no usable effect for this draw; leave the guest's shadows intact
    auto *profiler = GpuProfiler::Get();
    const u64 previous = profiler ? profiler->Current() : 0;
    if (profiler)
        profiler->Mark(0x434F4E54414354ull, [] { return std::string("contact shadows / PCSS"); });
    scheduler.Record([push, width, height, target_view, depth_view, in, sampler = *sampler,
                      pipeline = *pipeline, layout = *layout](vk::CommandBuffer cmd) {
        const vk::MemoryBarrier2 before{.srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
                                        .srcAccessMask = vk::AccessFlagBits2::eMemoryWrite,
                                        .dstStageMask = Compute,
                                        .dstAccessMask = Read | Write};
        cmd.pipelineBarrier2({.memoryBarrierCount = 1, .pMemoryBarriers = &before});
        const vk::DescriptorImageInfo depth_info{sampler, depth_view, vk::ImageLayout::eGeneral};
        const vk::DescriptorImageInfo target_info{{}, target_view, vk::ImageLayout::eGeneral};
        const std::array<vk::WriteDescriptorSet, 5> writes{
            {{.dstBinding = 0,
              .descriptorCount = 1,
              .descriptorType = vk::DescriptorType::eStorageBuffer,
              .pBufferInfo = &in.constants},
             {.dstBinding = 1,
              .descriptorCount = 1,
              .descriptorType = vk::DescriptorType::eCombinedImageSampler,
              .pImageInfo = &depth_info},
             {.dstBinding = 2,
              .descriptorCount = 1,
              .descriptorType = vk::DescriptorType::eStorageImage,
              .pImageInfo = &target_info},
             {.dstBinding = 3,
              .descriptorCount = 1,
              .descriptorType = vk::DescriptorType::eCombinedImageSampler,
              .pImageInfo = &in.shadow_map},
             {.dstBinding = 4,
              .descriptorCount = 1,
              .descriptorType = vk::DescriptorType::eStorageBuffer,
              .pBufferInfo = &in.uniforms}}};
        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline);
        cmd.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute, layout, 0, writes);
        cmd.pushConstants(layout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(Push), &push);
        cmd.dispatch((width + 7) / 8, (height + 7) / 8, 1);
        const vk::MemoryBarrier2 after{.srcStageMask = Compute,
                                       .srcAccessMask = Write,
                                       .dstStageMask = vk::PipelineStageFlagBits2::eAllCommands,
                                       .dstAccessMask = vk::AccessFlagBits2::eMemoryRead |
                                                        vk::AccessFlagBits2::eMemoryWrite};
        cmd.pipelineBarrier2({.memoryBarrierCount = 1, .pMemoryBarriers = &after});
    });
    if (profiler)
        profiler->Resume(previous);
    if (frame == 1)
        std::printf("Contact shadows/PCSS: %ux%u, contacts %u, PCSS %u\n", width, height,
                    push.contact, push.pcss);
}
} // namespace Vulkan
