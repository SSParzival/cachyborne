// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <mutex>

namespace BbMenuPointer {
// The event thread scrolls; the guest thread updates lists. A stationary pointer
// (including a small twitch while using the wheel) must not reselect the row
// that just scrolled under it. A click always overrides the hold at the caller.
class WheelHover {
public:
    void Scroll(float x, float y) {
        std::lock_guard lock{mutex};
        held = true;
        origin_x = x;
        origin_y = y;
    }
    bool Holds(float x, float y) {
        std::lock_guard lock{mutex};
        const float dx = x - origin_x, dy = y - origin_y;
        if (held && dx * dx + dy * dy > 16.0f * 16.0f) held = false;
        return held;
    }
private:
    std::mutex mutex;
    bool held = false;
    float origin_x = 0, origin_y = 0;
};
} // namespace BbMenuPointer
