// SPDX-License-Identifier: GPL-2.0-or-later
// Cachyborne integration of Lantern Engine by Awaken's contact/PCSS shader.
#pragma once
#include "common/types.h"
#include "textures/image.h"

namespace VideoCore {
class TextureCache;
}
namespace Vulkan {
class Instance;
class Scheduler;
class Runtime;
class SceneTargets;
class CameraMotion;
class ContactShadow {
  public:
    static constexpr u64 ResolveShader = 0xf55643de, BlurShader = 0x11ffdfb5;
    struct Inputs {
        vk::DescriptorBufferInfo constants, uniforms;
        vk::DescriptorImageInfo shadow_map;
        u32 offsets = 0;
    };
    ContactShadow(const Instance &, Scheduler &, Runtime &, VideoCore::TextureCache &,
                  SceneTargets &, CameraMotion &);
    ~ContactShadow();
    static bool Wanted();
    // Guest sampled-image descriptors use a separate sampler binding. Rebuild
    // combined descriptors with our live sampler and disable PCSS on missing inputs.
    static Inputs SafeInputs(const Inputs &, const vk::DescriptorImageInfo &depth, bool &filter);
    void OnResolve(VideoCore::ImageId, const Inputs &);
    void OnBlur(VideoCore::ImageId, VAddr source);
    void Flush(bool blur_next);

  private:
    const Instance &instance;
    Scheduler &scheduler;
    Runtime &runtime;
    VideoCore::TextureCache &textures;
    SceneTargets &scenes;
    CameraMotion &camera;
    vk::UniqueDescriptorSetLayout descriptors;
    vk::UniquePipelineLayout layout;
    vk::UniquePipeline pipeline;
    vk::UniqueSampler sampler;
    VideoCore::ImageId pending{};
    u64 pending_uid = 0;
    Inputs inputs{};
    u32 frame = 0;
};
} // namespace Vulkan
