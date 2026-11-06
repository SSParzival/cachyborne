// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

namespace BbAppearance {
struct Stats { std::uint64_t opened,closed,updates,failed; bool enabled; };
// Runtime half of the optional Dream mirror. Setup also prepares its generated
// layout. Both are selected by the launcher's appearance enhancement setting.
bool Install(unsigned char* image,std::uint64_t size);
Stats Read();
}
