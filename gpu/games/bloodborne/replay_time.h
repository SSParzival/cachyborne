// SPDX-License-Identifier: GPL-3.0-or-later
// Adapted from bbhost v0.2.24 decomp/chr/{chr_sync,replay_manipulator}.cpp.
#pragma once
#include "bbport_frame_clock.h"
#include <cstdint>
#include <cstring>

namespace BbReplayTime {
using Address = std::uintptr_t;
template<class T> T Load(Address at) {
    T value;
    std::memcpy(&value, reinterpret_cast<const void*>(at), sizeof value);
    return value;
}
template<class T> void Store(Address at, T value) {
    std::memcpy(reinterpret_cast<void*>(at), &value, sizeof value);
}
using Sync = std::uint64_t (*)(Address, Address);
using Replay = std::uint64_t (*)(Address, Address, float);

inline std::uint64_t SyncUpdate(Address self, Address chr, BbFrameRate::ClockFrame frame, Sync game) {
    if (frame.tick30) return game(self, chr);
    const auto countdown = Load<std::int32_t>(self + 0xc8);
    Store<std::int32_t>(self + 0xc8, 2); // original update sees no send frame
    const auto result = game(self, chr);
    Store(self + 0xc8, countdown);
    return result;
}

inline std::uint64_t ReplayUpdate(Address self, Address chr, float step,
                                  BbFrameRate::ClockFrame frame, Address vtable, Replay game) {
    if (Load<Address>(self) != vtable) return game(self, chr, step);
    const Address recording = Load<Address>(self + 0xf0);
    const bool plays = recording && Load<std::int32_t>(recording + 0x90) > 0;
    const auto countdown = Load<std::int32_t>(self + 0xf8);
    const bool hold = !frame.tick30 && countdown > 0;
    if (hold) Store<std::int32_t>(self + 0xf8, 2);
    const auto result = game(self, chr, step);
    if (hold) Store(self + 0xf8, countdown);
    const float frames = frame.Frames30();
    if (!plays || frames == 1.0f) return result;
    // Large (>10-unit) teleports zero the tenth after leaving the whole move.
    // Scale only the ordinary move equal to that tenth, never the teleport.
    if (!std::memcmp(reinterpret_cast<const void*>(self + 0x70),
                     reinterpret_cast<const void*>(self + 0xd0), 16))
        for (unsigned k = 0; k < 16; k += 4)
            Store(self + 0x70 + k, Load<float>(self + 0xd0 + k) * frames);
    for (unsigned k = 0; k < 16; k += 4)
        Store(self + 0x20 + k, Load<float>(self + 0xe0 + k) * frames);
    return result;
}
inline bool BodyMatches(const void* code, std::size_t bytes, std::uint64_t expected) {
    auto* data = static_cast<const unsigned char*>(code);
    std::uint64_t hash = 0xcbf29ce484222325ull;
    for (std::size_t i = 0; i < bytes; ++i) hash = (hash ^ data[i]) * 0x100000001b3ull;
    return hash == expected;
}
void Install(unsigned char* image, std::uint64_t size);
} // namespace BbReplayTime
