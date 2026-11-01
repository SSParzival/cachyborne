// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>

namespace VideoCore {
using GuestImageReader = void (*)(std::uint64_t, void*, std::uint64_t);
/// Hash every byte through a backing reader, with bounded scratch storage.
std::uint64_t HashGuestImage(std::uint64_t address, std::uint64_t size, GuestImageReader read);
}
