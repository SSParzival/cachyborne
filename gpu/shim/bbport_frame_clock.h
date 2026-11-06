// SPDX-License-Identifier: GPL-3.0-or-later
// Game-time cadence adapted from bbhost v0.2.24 engine/frame_clock.h.
#pragma once
#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>

namespace BbFrameRate {
inline constexpr float Thirtieth = std::bit_cast<float>(0x3d088889u);
struct ClockFrame {
    float dt = Thirtieth;
    bool tick30 = true;
    float Frames30() const {
        return dt == Thirtieth ? 1.0f : float(30.0 * double(dt));
    }
};

// One writer, the game main loop before launching frame tasks. A packed atomic
// publishes dt and tick together; readers never combine two different frames.
class FrameClock {
public:
    void Publish(float dt) {
        dt = std::isfinite(dt) ? std::clamp(dt, 0.0f, Thirtieth) : 0.0f;
        const double sum = carry + ClockFrame{dt}.Frames30();
        const bool tick = sum > 0.0;
        carry = sum - int(tick);
        frame.store(std::uint64_t(std::bit_cast<std::uint32_t>(dt)) |
                    (std::uint64_t(tick) << 32), std::memory_order_release);
    }
    ClockFrame Read() const {
        const auto bits = frame.load(std::memory_order_acquire);
        return {std::bit_cast<float>(std::uint32_t(bits)), bool(bits >> 32)};
    }
private:
    double carry = 0;
    std::atomic<std::uint64_t> frame{0x13d088889ull};
};
inline FrameClock game_clock;
} // namespace BbFrameRate
