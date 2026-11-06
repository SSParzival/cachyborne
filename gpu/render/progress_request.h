// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>

namespace Vulkan {
// Heap-owned by both producer callback and guest waiter. Cancellation stops a
// queued flush; an already active flush can finish after its waiter times out.
class GpuProgressRequest {
public:
    bool Begin() {
        std::scoped_lock lock{mutex};
        return !cancelled;
    }
    void Complete(std::uint64_t tick) {
        {
            std::scoped_lock lock{mutex};
            completed_tick = tick;
        }
        cv.notify_one();
    }
    std::optional<std::uint64_t> WaitUntil(std::chrono::steady_clock::time_point deadline) {
        std::unique_lock lock{mutex};
        if (!cv.wait_until(lock, deadline, [this] { return completed_tick.has_value(); })) {
            cancelled = true;
            return std::nullopt;
        }
        return completed_tick;
    }
private:
    std::mutex mutex;
    std::condition_variable cv;
    bool cancelled = false;
    std::optional<std::uint64_t> completed_tick;
};
}
