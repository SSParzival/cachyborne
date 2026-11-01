// SPDX-FileCopyrightText: Copyright 2026 bbport contributors
// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: physical cores of their own for the draw pipeline's stages (bbport_affinity.h).
//
// Two physical cores are reserved: each stage thread runs on one hardware thread of its core and
// the sibling stays idle; every other thread of the process runs on the remaining cores. Threads
// come and go (the game's, Vulkan's, ours), so a background thread applies the placement once a
// second, by thread name. Measured in a heavy scene (Steam Deck save, 1500 draws a frame) on a
// Ryzen 7 5700X3D: 16 threads 133-138 FPS, 8 cores without SMT 152; 6 cores / 12 threads 130,
// without SMT 155.
#include "bbport_affinity.h"
#include "shim/bb_env.h"

#ifndef _WIN32
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <sched.h>
#include <unistd.h>

#include "bbport_threads.h"
#include "common/thread.h"
#include "../../src/cachy_affinity.h"
#endif

namespace BbAffinity {

#ifndef _WIN32
namespace {

constexpr const char* StageThreads[] = {"bb:GpuComm", "bb:DrawRec"};

// SDL's audio threads inherit the opening thread's mask, but Mesa queues may
// explicitly restore the startup mask. Port workers are placed at thread entry;
// library queues are reconciled here without changing unrelated guest threads.
void KeepHybrid() {
    Common::SetCurrentThreadName("bb:Affinity");
    for (;;) {
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator{"/proc/self/task", ec}) {
            const pid_t tid = pid_t(std::strtol(entry.path().filename().c_str(), nullptr, 10));
            if (tid <= 0) continue;
            std::string name;
            std::getline(std::ifstream{entry.path() / "comm"}, name);
            if (!cachy_mesa_worker(name.c_str()) &&
                !strcasestr(name.c_str(), "audio") && !strcasestr(name.c_str(), "fmod")) {
                continue;
            }
            cpu_set_t wanted, current;
            if (cachy_select_cpus(name.c_str(), &wanted) &&
                sched_getaffinity(tid, sizeof(current), &current) == 0 &&
                !CPU_EQUAL(&current, &wanted)) {
                // ESRCH is normal when a short-lived worker exits during the scan.
                sched_setaffinity(tid, sizeof(wanted), &wanted);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
}

/// The hardware threads of each physical core this process may run on (core -> CPUs).
std::map<std::string, std::vector<int>> PhysicalCores(const cpu_set_t& allowed) {
    std::map<std::string, std::vector<int>> cores;
    for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
        if (!CPU_ISSET(cpu, &allowed)) {
            continue;
        }
        const std::string base = "/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/topology/";
        std::string package, core;
        std::ifstream{base + "physical_package_id"} >> package;
        std::ifstream{base + "core_id"} >> core;
        if (core.empty()) {
            return {}; // no topology: left to the scheduler
        }
        cores[package + ":" + core].push_back(cpu);
    }
    return cores;
}

void Keep(std::vector<int> stage_cpus, cpu_set_t rest) {
    Common::SetCurrentThreadName("bb:Affinity");
    BbThreads::MakeBackground();
    for (;;) {
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator{"/proc/self/task", ec}) {
            const pid_t tid = pid_t(std::strtol(entry.path().filename().c_str(), nullptr, 10));
            // By the current name: a new thread carries its creator's until it names itself.
            std::string name;
            std::getline(std::ifstream{entry.path() / "comm"}, name);
            cpu_set_t wanted = rest;
            for (size_t i = 0; i < std::size(StageThreads); ++i) {
                if (name == StageThreads[i]) {
                    CPU_ZERO(&wanted);
                    CPU_SET(stage_cpus[i], &wanted);
                }
            }
            cpu_set_t current;
            CPU_ZERO(&current);
            if (sched_getaffinity(tid, sizeof(current), &current) == 0 &&
                !CPU_EQUAL(&current, &wanted)) {
                sched_setaffinity(tid, sizeof(wanted), &wanted);
            }
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

} // namespace
#endif

void Start() {
    // Cachyborne's explicit P/E policy owns placement; the upstream monitor must
    // not reset those masks once per second, including audio onto E cores.
#ifndef _WIN32
    if (bb_env_on("BB_CPU_PINNING")) {
        // Vulkan/SDL initialization happens after Start: their initial critical
        // threads inherit the foreground mask rather than the unrestricted one.
        static std::once_flag started;
        std::call_once(started, [] {
            cachy_pin_thread(pthread_self(), "main");
            std::thread{KeepHybrid}.detach();
        });
        return;
    }
    const char* env = bb_env("BB_CORE_RESERVE");
    if (env && env[0] == '0') {
        return;
    }
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    if (sched_getaffinity(0, sizeof(allowed), &allowed) != 0) {
        return;
    }
    const auto cores = PhysicalCores(allowed);
    bool smt = false;
    for (const auto& [_, cpus] : cores) {
        smt |= cpus.size() > 1;
    }
    // Without SMT the stages have whole cores anyway; with 4 cores, two reserved left the game's
    // own threads too little.
    const bool forced = env && env[0] == '1';
    if (!forced && (!smt || cores.size() < 6)) {
        return;
    }
    if (cores.size() < 3) {
        return;
    }
    // The second and third cores: next to the first (the same cache complex on chips with
    // several), which takes most interrupts.
    std::vector<int> stage_cpus;
    cpu_set_t rest = allowed;
    auto core = std::next(cores.begin());
    for (size_t i = 0; i < std::size(StageThreads); ++i, ++core) {
        stage_cpus.push_back(core->second.front());
        for (const int cpu : core->second) {
            CPU_CLR(cpu, &rest);
        }
    }
    std::printf("CPU: the draw pipeline's stages on cores of their own (CPUs %d and %d, siblings "
                "idle), %d other hardware threads for the rest (BB_CORE_RESERVE=0: off)\n",
                stage_cpus[0], stage_cpus[1], CPU_COUNT(&rest));
    std::thread{Keep, stage_cpus, rest}.detach();
#endif
}

} // namespace BbAffinity
