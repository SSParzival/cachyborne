// SPDX-License-Identifier: GPL-3.0-or-later
// Adapted from droogie/bbhost v0.2.24 src/decomp/gx/job_barrier.cpp.
#pragma once
#include <cstdint>
#include <cstring>

namespace BbGxBarrier {
using Address = std::uintptr_t;
template<class T> inline T Load(Address at) {
    T value;
    std::memcpy(&value, reinterpret_cast<const void*>(at), sizeof value);
    return value;
}
struct Operations {
    bool (*pop)(Address deque, Address* job);
    bool (*steal)(Address victim, Address thief);
    std::uint64_t (*tsc)();
};

// Deque locking belongs to the game's pop/steal functions. Never hold a host
// mutex across them or across a job: jobs may recursively wait on GX barriers.
inline bool Sweep(Address self, const Operations& ops) {
    const auto tsc = ops.tsc();
    Address system = Load<Address>(self + 8);
    auto count = Load<std::uint32_t>(system + 8);
    if (!count) return false;
    const auto from = std::uint64_t(Load<std::uint32_t>(self + 0xa0)) + tsc;
    const auto start = std::uint32_t(from % count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto at = (start + i) % count;
        const Address participant = Load<Address>(system) + Address(at) * 0x530;
        if (participant != self) {
            if (ops.steal(participant + 0x18, self + 0x18) || Load<Address>(self + 0x38))
                return true;
            system = Load<Address>(self + 8);
        }
        count = Load<std::uint32_t>(system + 8);
    }
    return false;
}

inline void Step(Address self, const Operations& ops, bool assist) {
    Address job = 0;
    if (!ops.pop(self + 0x18, &job) && Sweep(self, ops)) ops.pop(self + 0x18, &job);
    if (job) {
        using Run = void (*)(Address, std::uint32_t);
        reinterpret_cast<Run>(Load<Address>(Load<Address>(job)))(job, Load<std::uint32_t>(self + 0xa0));
    }
    // Leave the stolen jobs queued. The original barrier sees the nonempty
    // deque and invokes its step again instead of entering its idle spin.
    if (assist && !Load<Address>(self + 0x38)) Sweep(self, ops);
}

void Install();
} // namespace BbGxBarrier
