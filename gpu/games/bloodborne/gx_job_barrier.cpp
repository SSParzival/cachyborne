// SPDX-License-Identifier: GPL-2.0-or-later
#include "gx_job_barrier.h"
#include "bbport_guest_code_hooks.h"
#include "shim/bb_env.h"
#include <cstdio>

namespace BbGxBarrier {
namespace {
constexpr Address Image = 0x800000000ull;
// Upstream's Binary Ninja addresses include 0x400000; Cachyborne uses offsets.
constexpr Address Entry = 0x25b9980 - 0x400000;
constexpr std::uint8_t Signature[] = {
    0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec,0x18
};
const Operations ops = {
    reinterpret_cast<decltype(Operations::pop)>(Image + 0x1247b60 - 0x400000),
    reinterpret_cast<decltype(Operations::steal)>(Image + 0x1247d20 - 0x400000),
    reinterpret_cast<decltype(Operations::tsc)>(Image + 0x2fc0de8 - 0x400000),
};
void __attribute__((sysv_abi)) OnStep(std::uint64_t, const Core::GuestRegisterSnapshot* regs) {
    Step(regs->rdi, ops, true);
}
}
void Install() {
    if (bb_env_off("BB_GX_BARRIER_STEAL")) return;
    if (Core::InstallGuestCodeHook(reinterpret_cast<void*>(Image + Entry), Signature, 0,
                                   OnStep, nullptr, true))
        std::printf("GX barrier: main thread assists queued jobs (BB_GX_BARRIER_STEAL=0 disables)\n");
}
} // namespace BbGxBarrier
