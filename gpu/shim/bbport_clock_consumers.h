// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "bbport_frame_clock.h"
#include <cstring>

namespace BbFrameRate {
inline float YebisElapsed(float value, ClockFrame frame) {
    const float frames = frame.Frames30();
    return frames == 1.0f ? value : value * frames;
}
using KeyFrameStep = std::uint64_t (*)(std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t);
inline std::uint64_t DriveKeyFrame(std::uintptr_t keyframe, std::uintptr_t bodies,
                                   std::uintptr_t palette, std::uintptr_t indices,
                                   ClockFrame frame, KeyFrameStep game) {
    auto* field = reinterpret_cast<void*>(bodies + 0x10); // controller +0x60
    float step;
    std::memcpy(&step, field, sizeof step);
    const bool replace = std::bit_cast<std::uint32_t>(step) == 0x3d088889u && frame.dt != Thirtieth;
    if (replace) std::memcpy(field, &frame.dt, sizeof step);
    const auto result = game(keyframe, bodies, palette, indices);
    if (replace) std::memcpy(field, &step, sizeof step);
    return result;
}
} // namespace BbFrameRate
