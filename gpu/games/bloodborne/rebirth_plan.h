// SPDX-License-Identifier: GPL-3.0-or-later
// Pure reset/refund planning, adapted from BBHOST engine/rebirth.cpp at
// bdc5cdf8be5f37188da9c2368e92be444fdc04cb. No guest memory writes or polling.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>
#include <cstring>

namespace BbRebirth {
constexpr std::int64_t EchoCap = 999999999;
constexpr std::int32_t MaxLevel = 544;
struct Build {
    std::int32_t level = 0;
    std::array<std::int32_t, 6> stats{};
    std::int64_t echoes = 0;
    bool operator==(const Build&) const = default;
};
struct Origin {
    std::int32_t level = 0;
    std::array<std::int32_t, 6> stats{};
};
using CostGraph = std::array<float, 4>;
struct ResetPlan {
    Build before, after;
    std::int64_t refund;
    std::int32_t bought_points;
};
inline bool ValidStats(const std::array<std::int32_t, 6>& stats) {
    return std::all_of(stats.begin(), stats.end(), [](auto s) { return s >= 1 && s <= 99; });
}
// The game's origin row uses a signed 16-bit level and six noncontiguous bytes.
inline std::optional<Origin> DecodeOrigin(std::span<const std::uint8_t> row) {
    if (row.size() < 0xc9) return {};
    std::int16_t level;
    std::memcpy(&level, row.data() + 0xc0, sizeof(level));
    Origin out{level, {}};
    constexpr std::size_t offsets[] = {0xc2, 0xc3, 0xc5, 0xc6, 0xc7, 0xc8};
    for (std::size_t i = 0; i < out.stats.size(); ++i) out.stats[i] = row[offsets[i]];
    if (out.level < 1 || out.level > MaxLevel || !ValidStats(out.stats)) return {};
    return out;
}
inline std::optional<CostGraph> DecodeGraph(std::span<const std::uint8_t> row) {
    if (row.size() < 0x4c) return {};
    CostGraph out;
    std::memcpy(out.data(), row.data() + 0x3c, sizeof(out));
    if (!std::all_of(out.begin(), out.end(), [](float f) { return std::isfinite(f); })) return {};
    return out;
}
inline std::optional<std::int64_t> LevelCost(std::int32_t level, const CostGraph& graph) {
    if (level < 1 || level > MaxLevel ||
        !std::all_of(graph.begin(), graph.end(), [](float f) { return std::isfinite(f); })) return {};
    const float x = static_cast<float>(level + 81);
    // Retail uses separate scalar multiplies/adds and rounds twice. Prevent
    // native FMA contraction from changing the affordability threshold.
    const float k = std::max(x - graph[3], 0.f);
    const volatile float slope = k * graph[2];
    const float adjusted = slope + graph[0];
    const volatile float curve = (x * x) * adjusted;
    const float price = (curve + graph[1]) + .5f;
    // Validate in floating point before narrowing; damaged/modded graphs must
    // never cause undefined float-to-integer conversion or overflow a refund.
    if (!std::isfinite(price) || price < 0 || static_cast<double>(price) > EchoCap) return {};
    const auto first = static_cast<std::int32_t>(price);
    const float second = static_cast<float>(first) + .5f;
    if (static_cast<double>(second) > EchoCap) return {};
    return static_cast<std::int64_t>(second);
}
inline std::optional<ResetPlan> PlanReset(const Build& current, const Origin& origin, const CostGraph& graph) {
    if (current.level < 1 || current.level > MaxLevel || origin.level < 1 || origin.level > MaxLevel ||
        !ValidStats(current.stats) || !ValidStats(origin.stats) || current.echoes < 0 || current.echoes > EchoCap) return {};
    std::int32_t gained = 0;
    for (std::size_t i = 0; i < current.stats.size(); ++i)
        gained += std::max(current.stats[i] - origin.stats[i], 0);
    if (origin.level + gained > MaxLevel) return {};
    std::int64_t refund = 0;
    for (std::int32_t level = origin.level; level < origin.level + gained; ++level) {
        const auto cost = LevelCost(level, graph);
        if (!cost || *cost > EchoCap - current.echoes - refund) return {};
        refund += *cost;
    }
    // Validate even the no-levels case, without dereferencing guest records.
    if (!LevelCost(origin.level, graph)) return {};
    return ResetPlan{current, Build{origin.level, origin.stats, current.echoes + refund}, refund, gained};
}
} // namespace BbRebirth
