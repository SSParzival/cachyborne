// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_guest_guards.h"
#include <cassert>
#include <cstring>
#include <sys/mman.h>

int main() {
    using namespace BbGuestCode;
    auto* code = static_cast<u8*>(
        mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    assert(code != MAP_FAILED);
    const auto guard =
        OnlineEventGuard(reinterpret_cast<u64>(code + 512), reinterpret_cast<u64>(code + 768));
    assert(!guard.overflow);
    std::memcpy(code, guard.code.data(), guard.size);
    Emitter result;
    result.Bytes({0x4c, 0x89, 0xe0, 0x5d, 0x41, 0x5c, 0xc3}); // rax=r12; restore frame; ret
    std::memcpy(code + 512, result.code.data(), result.size);
    Emitter unknown;
    unknown.Bytes({0x45, 0x31, 0xe4}); // The game's unknown-event result: r12=0.
    unknown.JmpAbs(reinterpret_cast<u64>(code + 512));
    std::memcpy(code + 768, unknown.code.data(), unknown.size);
    Emitter entry;
    entry.Bytes({0x41, 0x54, 0x55, 0x49, 0x89, 0xfc, 0x48, 0x89, 0xf5});
    // Preserve host callee-saved r12/rbp, set the guest payload and local-frame registers.
    entry.JmpAbs(reinterpret_cast<u64>(code));
    std::memcpy(code + 1024, entry.code.data(), entry.size);
    assert(mprotect(code, 4096, PROT_READ | PROT_EXEC) == 0);
    const auto run = reinterpret_cast<u64 (*)(u64, u64)>(code + 1024);
    assert(run(0, 0x1000) == 0);
    assert(run(0x10000, 0x1000) == 0x10080);
    assert(run(1, 0x1000) == 129);
    munmap(code, 4096);
}
