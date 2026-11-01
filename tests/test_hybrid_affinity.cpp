// SPDX-License-Identifier: GPL-2.0-or-later
// Exercise the production reconciler with library threads that reset affinity.
#include "shim/bbport_affinity.h"
#include "common/thread.h"
#include "../src/cachy_affinity.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <thread>

namespace Common {
void SetCurrentThreadName(const char* name) {
    pthread_setname_np(pthread_self(), name);
    cachy_pin_thread(pthread_self(), name);
}
}

int main() {
    cpu_set_t original;
    assert(sched_getaffinity(0, sizeof(original), &original) == 0);
    int first = -1, last = -1;
    for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
        if (CPU_ISSET(cpu, &original)) {
            if (first < 0) first = cpu;
            last = cpu;
        }
    }
    assert(first >= 0);
    setenv("BB_CPU_PINNING", "1", 1);
    setenv("BB_P_CPUS", std::to_string(first).c_str(), 1);
    setenv("BB_E_CPUS", std::to_string(last).c_str(), 1);
    BbAffinity::Start();
    BbAffinity::Start(); // Initialization is idempotent.
    cpu_set_t main_mask;
    assert(sched_getaffinity(0, sizeof(main_mask), &main_mask) == 0);
    assert(CPU_COUNT(&main_mask) == 1 && CPU_ISSET(first, &main_mask));
    std::atomic<int> ready{0};
    std::atomic<bool> stop{false};
    const auto worker = [&](const char* name) {
        // Bypass Common, like a library; reset the inherited foreground mask.
        assert(pthread_setname_np(pthread_self(), name) == 0);
        assert(sched_setaffinity(0, sizeof(original), &original) == 0);
        ready.fetch_add(1);
        while (!stop.load()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    };
    std::thread disk(worker, "bb-probe:disk$0");
    std::thread audio(worker, "SDLAudioP1");
    std::thread unrelated(worker, "other-library");
    while (ready.load() != 3) std::this_thread::yield();
    const auto matches = [](std::thread& thread, int cpu) {
        cpu_set_t mask;
        return pthread_getaffinity_np(thread.native_handle(), sizeof(mask), &mask) == 0 &&
               CPU_COUNT(&mask) == 1 && CPU_ISSET(cpu, &mask);
    };
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while ((!matches(disk, last) || !matches(audio, first)) &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    assert(matches(disk, last) && matches(audio, first));
    cpu_set_t other_mask;
    assert(pthread_getaffinity_np(unrelated.native_handle(), sizeof(other_mask), &other_mask) == 0);
    assert(CPU_EQUAL(&original, &other_mask));
    stop.store(true);
    disk.join(); audio.join(); unrelated.join();
}
