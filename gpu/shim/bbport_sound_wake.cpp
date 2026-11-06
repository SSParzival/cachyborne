// SPDX-License-Identifier: GPL-3.0-or-later
#include "bbport_sound_wake.h"
#include "bbport_guest_code.h"
#include "bbport_guest_code_hooks.h"
#include "bbport_map_near.h"
#include "bb_env.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <sys/mman.h>

namespace BbSoundWake {
namespace {
// Leave the first six bytes to the offline acceptance observer. This hook is
// immediately after it, still before the limiter and any pacer state changes.
constexpr std::uint64_t Setup = 0x2801373, Call = 0x280137d, Pacer = 0x2034776;
constexpr unsigned char SetupBytes[] = {0x8b,0xbb,0xd4,0x08,0x00,0x00,0x29,0xc7,0x76,0x05};
constexpr unsigned char CallBytes[] = {0xe8,0x3e,0xa7,0x88,0xff}; // target image+0x208bac0
constexpr unsigned char PacerBytes[] = {0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec,0x38};
std::atomic<bool> enabled{false};
FrameWake& Wake() {
    // Guest threads can remain alive during host exit callbacks. No private
    // worker is spawned here; retain its mutex/CV until process reclamation.
    static auto* const wake = new FrameWake;
    return *wake;
}
int __attribute__((sysv_abi)) Sleep(std::uint32_t asked) {
    Wake().Wait(static_cast<std::int32_t>(asked));
    return 0;
}
void OnFrame(std::uint64_t, const Core::GuestRegisterSnapshot*) {
    if (!enabled.load(std::memory_order_acquire)) return;
    auto& wake = Wake();
    wake.Frame();
    static const bool log = bb_env_on("BB_SOUND_WAKE_LOG");
    if (log) {
        const auto stats = wake.Read();
        if (stats.frames % 300 == 0) {
            std::printf("Sound wake: %llu frames, %llu passes, %llu timeouts\n",
                        (unsigned long long)stats.frames, (unsigned long long)stats.passes,
                        (unsigned long long)stats.timeouts);
        }
    }
}
}

bool Install(unsigned char* image, std::uint64_t size) {
    if (bb_env_off("BB_SOUND_WAKE")) return false;
    if (enabled.load(std::memory_order_acquire)) return true;
    // Preserve the game's 33333 us fallback and 33000 us user-event poll.
    if (size < 0x4733c56 || std::memcmp(image + Setup, SetupBytes, sizeof SetupBytes) ||
        std::memcmp(image + Call, CallBytes, sizeof CallBytes) ||
        std::memcmp(image + Pacer, PacerBytes, sizeof PacerBytes) ||
        std::memcmp(image + 0x4733c51, "33333", 5)) return false;
    const auto base = reinterpret_cast<std::uintptr_t>(image);
    auto* stub = static_cast<unsigned char*>(BbMapNear::Map(base,base+size,4096,
        PROT_READ|PROT_WRITE,64ull<<20,"Sound wake"));
    if (!stub) return false;
    BbGuestCode::Emitter code;
    code.JmpAbs(reinterpret_cast<std::uintptr_t>(&Sleep));
    std::memcpy(stub,code.code.data(),code.size);
    const auto distance = reinterpret_cast<std::intptr_t>(stub) -
                          reinterpret_cast<std::intptr_t>(image + Call + 5);
    const auto relative = static_cast<std::int32_t>(distance);
    auto* call_page = reinterpret_cast<void*>((base+Call)&~std::uintptr_t(4095));
    if (distance != relative || mprotect(stub,4096,PROT_READ|PROT_EXEC)) {
        munmap(stub,4096);
        return false;
    }
    Wake(); // before either callback can run
    if (mprotect(call_page,4096,PROT_READ|PROT_WRITE|PROT_EXEC)) {
        munmap(stub,4096);
        return false;
    }
    // Observer runs before the limiter, after this frame's requests were
    // queued. Generated/presented frames never notify the sound producer.
    if (!Core::InstallGuestCodeHook(image+Pacer,PacerBytes,0,OnFrame)) {
        mprotect(call_page,4096,Core::GuestCodeProtection());
        munmap(stub,4096);
        return false;
    }
    std::memcpy(image+Call+1,&relative,4); // retain CALL's ABI/return address
    mprotect(call_page,4096,Core::GuestCodeProtection());
    enabled.store(true,std::memory_order_release);
    std::puts("Sound wake: MagicOrchestra follows simulation frames before limiter; original sleep timeout retained");
    return true;
}
bool Enabled() { return enabled.load(std::memory_order_acquire); }
Stats Read() { return Wake().Read(); }
} // namespace BbSoundWake
