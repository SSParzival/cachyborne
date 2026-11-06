// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <chrono>

namespace BbVideoOut::Pacing {
using Clock=std::chrono::steady_clock;
enum class Policy { Relative, CatchUp, Phase };

inline Clock::time_point NextDeadline(Clock::time_point previous,Clock::time_point now,
                                      std::chrono::nanoseconds period,Policy policy) {
    if (period.count()<=0) return now;
    if (policy==Policy::CatchUp) return std::max(previous+period,now-period);
    if (policy==Policy::Phase) {
        // Keep phase through ordinary host wake delay. A genuine stall starts
        // a new schedule so the next frame does not rush to an old deadline.
        const auto tolerance=std::min(period/16,std::chrono::nanoseconds(1'000'000));
        if (now<=previous+tolerance) return previous+period;
    }
    return std::max(previous,now)+period;
}
}
