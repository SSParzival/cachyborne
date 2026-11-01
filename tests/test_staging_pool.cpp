// SPDX-License-Identifier: GPL-2.0-or-later
#include "render/scheduler.h"
#include "vulkan/instance.h"
#include "vulkan/staging_pool.h"
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <thread>

int main() {
    setenv("BB_STAGING_HOST_IDLE_MS", "1", 1);
    Vulkan::Instance instance(0, false);
    Vulkan::Scheduler scheduler(instance);
    Vulkan::StagingBufferPool pool(instance, scheduler);
    using VideoCore::MemoryType;
    constexpr u64 size = 20ull << 20;
    const auto pause = [] { std::this_thread::sleep_for(std::chrono::milliseconds(5)); };
    const auto allocation = pool.Request(size, MemoryType::HostCached, 256, true);
    assert(allocation.mapped && pool.BytesAllocated(MemoryType::HostCached) == size);
    // Wall time alone cannot release a deferred readback's storage.
    scheduler.Finish();
    pause();
    pool.TickFrame();
    assert(pool.BytesAllocated(MemoryType::HostCached) == size);
    pool.FreeDeferred(allocation);
    pause();
    pool.TickFrame();
    // FreeDeferred stamps the current submission: until it finishes, no trim.
    assert(pool.BytesAllocated(MemoryType::HostCached) == size);
    scheduler.Finish();
    pool.TickFrame();
    assert(pool.BytesAllocated(MemoryType::HostCached) == 0);
    // Ordinary whole staging is also protected until its submission completes.
    const auto upload = pool.Request(size, MemoryType::HostUncached);
    assert(upload.mapped);
    const u64 capacity = pool.BytesAllocated(MemoryType::HostUncached);
    pause();
    pool.TickFrame();
    assert(pool.BytesAllocated(MemoryType::HostUncached) == capacity);
    scheduler.Finish();
    pool.TickFrame();
    assert(pool.BytesAllocated(MemoryType::HostUncached) == capacity - size);
}
