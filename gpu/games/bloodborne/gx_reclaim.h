// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

namespace BbGxReclaim {
using Progress = bool (*)(unsigned stage, std::uint64_t timeout_ns);
struct Stats {
    bool installed, waiting;
    std::uint64_t copy_slow_paths, copy_failures, recovered, exhausted;
    std::uint64_t reclaims, pool_waits, pool_recovered, steps, waited_ns;
};
// The callback seam allows running the original retail code with controlled
// progress in regression tests. nullptr uses the renderer's bounded bridge.
bool Install(unsigned char* image, std::uint64_t size, Progress progress = nullptr);
bool CollectorInstalled();
Stats Read();
}
