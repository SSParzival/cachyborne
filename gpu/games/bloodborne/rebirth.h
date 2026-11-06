// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
namespace BbRebirth {
struct Stats { std::uint64_t resets,undos,failures;bool enabled; };
bool Install(unsigned char* image,std::uint64_t size);
Stats Read();
}
