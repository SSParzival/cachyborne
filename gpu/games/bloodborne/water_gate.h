// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
namespace BbWaterGate {
// Opt-in only until native water/visual acceptance. Install before guest runs.
bool Install(unsigned char* image, std::uint64_t size);
struct Stats {
    std::uint64_t frames, steps, camera_steps, held, grids_restored;
    std::uint64_t impulses_kept, impulses_replaced, impulses_lost;
};
Stats Read();
}
