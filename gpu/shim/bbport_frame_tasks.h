// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>

namespace BbFrameTasks {
using Task = void (*)();
// Register process-lifetime callbacks on the game's pacer, before its limiter.
// Registration is serialized and publication is safe while older tasks run.
// A callback is registered once; it must not throw or outlive its state.
bool Subscribe(unsigned char* image, std::uint64_t size, Task task);
}
