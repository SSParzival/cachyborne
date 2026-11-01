// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "bbport_guest_code.h"

namespace BbGuestCode {
inline constexpr std::array<u8, 11> OnlineEventBytes{0x49, 0x83, 0xec, 0x80, 0x48, 0x8d,
                                                     0xbd, 0x60, 0xff, 0xff, 0xff};
/// A missing case-0 event payload takes the game's existing unknown-event path.
inline Emitter OnlineEventGuard(u64 next, u64 unknown) {
    Emitter e;
    e.Bytes({0x4d, 0x85, 0xe4, 0x75, 0x0e}); // test r12,r12; jnz past the absolute jump
    e.JmpAbs(unknown);
    for (const auto b : OnlineEventBytes)
        e.Bytes({b});
    e.JmpAbs(next);
    return e;
}
} // namespace BbGuestCode
