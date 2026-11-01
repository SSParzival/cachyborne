// SPDX-License-Identifier: GPL-2.0-or-later
#include "textures/guest_image_hash.h"
#include <algorithm>
#include <array>
#define XXH_STATIC_LINKING_ONLY
#include <xxhash.h>

namespace VideoCore {
std::uint64_t HashGuestImage(std::uint64_t address, std::uint64_t size, GuestImageReader read) {
    XXH3_state_t state;
    XXH3_64bits_reset(&state);
    std::array<unsigned char, 4096> bytes;
    for (std::uint64_t offset = 0; offset < size;) {
        const auto count = std::min<std::uint64_t>(bytes.size(), size - offset);
        read(address + offset, bytes.data(), count);
        XXH3_64bits_update(&state, bytes.data(), count);
        offset += count;
    }
    return XXH3_64bits_digest(&state);
}
}
