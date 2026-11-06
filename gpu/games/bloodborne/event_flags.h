// SPDX-License-Identifier: GPL-2.0-or-later
// Shared retail event-flag entry points; callers run on the game's main thread.
#pragma once
#include <cstdint>
#include <span>
namespace BbEventFlags {
bool Validate(unsigned char* image,std::uint64_t size);
bool Read(std::uintptr_t image,std::span<const std::uint32_t> ids,std::span<bool> values);
bool Set(std::uintptr_t image,std::uint32_t id,bool value);
}
