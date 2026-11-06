// SPDX-License-Identifier: GPL-2.0-or-later
// Exercise actual Vulkan timeouts and completion publication, not a mock clock.
#include "vulkan/instance.h"
#include "vulkan/timeline.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <thread>

int main() {
    Vulkan::Instance instance(0, false);
    static vk::detail::DynamicLoader loader;
    vk::detail::DispatchLoaderDynamic dispatch;
    dispatch.init(loader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr"));
    dispatch.init(instance.GetInstance());
    dispatch.init(instance.GetDevice());
    Vulkan::Semaphore timeline(instance);
    assert(timeline.WaitFor(0, 0));
    assert(!timeline.WaitFor(1, 0));
    const auto start = std::chrono::steady_clock::now();
    assert(!timeline.WaitFor(1, 1000000));
    assert(std::chrono::steady_clock::now() - start < std::chrono::seconds(1));
    assert(timeline.KnownGpuTick() == 0);
    const auto signal = [&](u64 value) {
        const vk::SemaphoreSignalInfo info{.semaphore = timeline.Handle(), .value = value};
        assert(instance.GetDevice().signalSemaphore(&info, dispatch) == vk::Result::eSuccess);
    };
    signal(1);
    assert(timeline.WaitFor(1, 1000000000));
    assert(timeline.IsFree(1));
    assert(!timeline.WaitFor(2, 0));
    assert(timeline.KnownGpuTick() == 1);
    std::thread producer([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        signal(2);
    });
    assert(timeline.WaitFor(2, 1000000000));
    producer.join();
    assert(timeline.IsFree(2));
    // The legacy unbounded API must retain its publication guarantee too.
    std::thread final_producer([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        signal(3);
    });
    timeline.Wait(3);
    final_producer.join();
    assert(timeline.IsFree(3));
    assert(timeline.WaitFor(1, 0));
    assert(timeline.KnownGpuTick() >= 3);
    std::puts("PASS: real Vulkan bounded waits, timeout without false progress, delayed signal and legacy wait");
}
