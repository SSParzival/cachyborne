// SPDX-License-Identifier: GPL-2.0-or-later
// Real worker, real hashing/history: idle wake, stop during Check and queued
// cancellation.
#include "render/copy_filter.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <pthread.h>
#include <sys/mman.h>

namespace Vulkan {
struct CopyFilterTestAccess {
    static u64 Processed(const CopyFilterThread& worker) {
        return worker.processed.load(std::memory_order_acquire);
    }
    static clockid_t CpuClock(CopyFilterThread& worker) {
        clockid_t clock{};
        assert(pthread_getcpuclockid(worker.thread.native_handle(), &clock) == 0);
        return clock;
    }
    static bool Joined(const CopyFilterThread& worker) { return !worker.thread.joinable(); }
    static u64 Posted(const CopyFilterThread& worker) { return worker.head.load(); }
};
} // namespace Vulkan

using Vulkan::CopyEntry;
using Vulkan::CopyFilterThread;
using Access = Vulkan::CopyFilterTestAccess;
using Clock = std::chrono::steady_clock;

static void WaitProcessed(const CopyFilterThread& worker, u64 count) {
    const auto end = Clock::now() + std::chrono::seconds(10);
    while (Access::Processed(worker) < count) {
        assert(Clock::now() < end);
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
}

int main() {
    std::array<u8, 1024> source{}, destination{};
    source.fill(0x6d);
    std::array<u32, 3> control{0, 0, source.size()};
    const CopyEntry entry{u64(source.data()), u64(destination.data()), source.size(),
                          u64(control.data())};
    {
        CopyFilterThread worker;
        const auto run = [&](u64 packet, u32 kept) {
            std::vector<CopyEntry> entries{entry};
            worker.Post(packet, entries, entry.src, entry.dst);
            WaitProcessed(worker, packet);
            bool unchecked = true;
            const auto* job = worker.Take(packet, unchecked);
            assert(job && !unchecked && job->packet == packet && job->kept == kept);
            assert(job->epoch == worker.Epoch());
            assert(job->writes.size() == 1 && job->writes[0].first == entry.dst);
            assert(job->writes[0].second == source.size());
            worker.Release();
        };
        run(1, 1);
        std::memcpy(destination.data(), source.data(), source.size());
        run(2, 0); // a previous copy really completed and both byte ranges match
        destination[7] ^= 1;
        run(3, 1); // matching source hash alone cannot elide a changed destination
        std::memcpy(destination.data(), source.data(), source.size());
        worker.Invalidate();
        run(4, 1); // an unrecorded copy must invalidate the previous history
        worker.Stop();
        worker.Stop();
        assert(Access::Joined(worker));
    }

    // Stop must change the atomic wait value even when no packet was ever posted.
    // Repetition also races a first publication against startup and shutdown.
    for (unsigned cycle = 0; cycle < 1000; ++cycle) {
        CopyFilterThread worker;
        if (cycle & 1) {
            std::vector<CopyEntry> entries{entry};
            worker.Post(1, entries, entry.src, entry.dst);
        }
        if (cycle % 3 == 0) {
            continue; // exercise destructor-owned stop too, including pending publications
        }
        worker.Stop();
        assert(Access::Joined(worker));
        assert(Access::Processed(worker) <= Access::Posted(worker));
    }

    constexpr std::size_t Size = 8 * 1024 * 1024;
    auto* memory = static_cast<u8*>(
        mmap(nullptr, Size * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    assert(memory != MAP_FAILED);
    std::memset(memory, 0x35, Size * 2);
    {
        CopyFilterThread worker;
        const auto cpu = Access::CpuClock(worker);
        const CopyEntry large{u64(memory), u64(memory + Size), Size, u64(control.data())};
        // Eight GiB hashed through a 16 MiB allocation: long enough to observe an
        // in-flight check without adding a hook or replacing the worker's
        // algorithm.
        std::vector<CopyEntry> entries(1024, large);
        worker.Post(1, entries, large.src, large.dst);
        entries.assign(1, large);
        worker.Post(2, entries, large.src, large.dst);
        const auto deadline = Clock::now() + std::chrono::seconds(10);
        for (;;) {
            timespec used{};
            assert(clock_gettime(cpu, &used) == 0);
            if (used.tv_sec || used.tv_nsec >= 2'000'000) {
                break;
            }
            assert(Clock::now() < deadline);
            std::this_thread::yield();
        }
        assert(Access::Processed(worker) == 0); // first real check still in flight
        worker.Stop();
        assert(Access::Joined(worker));
        assert(Access::Processed(worker) == 1); // in flight finished, queued job cancelled
        // These mappings would now fault on any late access by a detached checker.
        assert(mprotect(memory, Size * 2, PROT_NONE) == 0);
    }
    assert(munmap(memory, Size * 2) == 0);
    std::puts("Copy filter: data/history checks, 1000 shutdown races, in-flight "
              "join and queued cancellation OK");
}
