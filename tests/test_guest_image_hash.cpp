// SPDX-License-Identifier: GPL-2.0-or-later
#include "textures/guest_image_hash.h"
#include <xxhash.h>
#include <algorithm>
#include <cassert>
#include <cstring>
#include <vector>

namespace {
constexpr std::uint64_t Base = 0x800000000ull;
std::vector<unsigned char> backing;
std::uint64_t read_bytes = 0, max_read = 0;
void Read(std::uint64_t address, void* data, std::uint64_t size) {
    assert(address >= Base && address - Base + size <= backing.size());
    assert(size <= 4096);
    read_bytes += size;
    max_read = std::max(max_read, size);
    std::memcpy(data, backing.data() + address - Base, size);
}
}

int main() {
    for (std::size_t size : {0u, 1u, 255u, 4095u, 4096u, 4097u, 1048593u}) {
        backing.resize(size);
        for (std::size_t i = 0; i < size; ++i) backing[i] = static_cast<unsigned char>(i * 37);
        read_bytes = max_read = 0;
        const auto hash = VideoCore::HashGuestImage(Base, size, Read);
        assert(read_bytes == size && max_read <= 4096);
        // An independent one-shot implementation verifies chunk boundaries and tails.
        assert(hash == XXH3_64bits(backing.data(), size));
        if (size > 256) {
            backing.back() ^= 0x80;
            assert(VideoCore::HashGuestImage(Base, size, Read) != hash);
            backing.back() ^= 0x80;
            assert(VideoCore::HashGuestImage(Base, size, Read) == hash);
        }
    }
}
