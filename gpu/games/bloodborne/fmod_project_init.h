// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

namespace BbFmodProjectInit {
// Explicit opt-in while broader failed-bank loading is still being tested.
// Success includes the disabled no-op. False means a requested fix is unavailable.
bool Install(unsigned char* image, std::uint64_t size);
struct Stats { bool enabled; std::uint64_t initialized; };
Stats Read();
}
