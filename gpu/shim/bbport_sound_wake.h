// SPDX-License-Identifier: GPL-3.0-or-later
// Frame-driven MagicOrchestra passes, adapted from bbhost v0.2.24 sound_wake.
#pragma once
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace BbSoundWake {
struct Stats {
    std::uint64_t frames = 0, consumed_frames = 0, passes = 0, timeouts = 0;
    bool waiting = false;
};

// One sound consumer, one frame producer. Notifications are a generation,
// not a queue: a pass handles all requests accumulated since its previous pass.
class FrameWake {
public:
    void Frame() {
        {
            std::lock_guard lock{mutex};
            ++stats.frames;
        }
        cv.notify_one();
    }
    void Wait(std::int32_t asked_us) {
        std::unique_lock lock{mutex};
        ++stats.passes;
        if (stats.consumed_frames == stats.frames) {
            stats.waiting = true;
            const auto duration = std::chrono::microseconds(std::clamp(asked_us, 0, 100000));
            if (!cv.wait_for(lock, duration, [&] { return stats.consumed_frames != stats.frames; })) {
                ++stats.timeouts;
            }
            stats.waiting = false;
        }
        stats.consumed_frames = stats.frames;
    }
    Stats Read() const {
        std::lock_guard lock{mutex};
        return stats;
    }
private:
    mutable std::mutex mutex;
    std::condition_variable cv;
    Stats stats;
};

// Startup only, before guest threads execute. Refuses a mismatched sound loop,
// period, call target or pacer entry. Caller gates this on the native clock.
bool Install(unsigned char* image, std::uint64_t size);
bool Enabled();
Stats Read();
} // namespace BbSoundWake
