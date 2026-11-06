// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

namespace BbOcclusionFresh {
// Clear a reused query block only after the original writer accepted the begin.
// The original PM4 packets and GPU reset remain unchanged.
bool Install(unsigned char* image, std::uint64_t size);
}
