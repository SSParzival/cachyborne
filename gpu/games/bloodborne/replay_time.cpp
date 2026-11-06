// SPDX-License-Identifier: GPL-3.0-or-later
#include "replay_time.h"
#include "bbport_guest_code_hooks.h"
#include "shim/bb_env.h"
#include <cstdio>

namespace BbReplayTime {
namespace {
void* original_sync = nullptr;
void* original_replay = nullptr;
Address replay_vtable = 0;
constexpr std::uint8_t Entry[] = {0x55,0x48,0x89,0xe5,0x41,0x57};
void __attribute__((sysv_abi)) OnSync(std::uint64_t, const Core::GuestRegisterSnapshot* regs) {
    const auto result = SyncUpdate(regs->rdi, regs->rsi, BbFrameRate::game_clock.Read(),
                                   reinterpret_cast<Sync>(original_sync));
    const_cast<Core::GuestRegisterSnapshot*>(regs)->rax = result;
}
void __attribute__((sysv_abi)) OnReplay(std::uint64_t, const Core::GuestRegisterSnapshot* regs) {
    float dt;
    std::memcpy(&dt, regs->ymm[0].data(), sizeof dt);
    const auto result = ReplayUpdate(regs->rdi, regs->rsi, dt, BbFrameRate::game_clock.Read(),
                                     replay_vtable, reinterpret_cast<Replay>(original_replay));
    const_cast<Core::GuestRegisterSnapshot*>(regs)->rax = result;
}
}

void Install(unsigned char* image, std::uint64_t size) {
    // Body hashes, including the original countdowns and tenths, are from the
    // 1.09 retail function verified by bbhost's differential tests. Refuse a
    // patched body instead of mixing its per-rate tables with game-time scaling.
    constexpr Address SyncAt = 0x1529470, SyncEnd = 0x15296c2;
    constexpr Address ReplayAt = 0x152c390, ReplayEnd = 0x152c930;
    if (size < ReplayEnd) return;
    bool sync = false, replay = false;
    if (!bb_env_off("BB_CHR_SYNC_STEP") &&
        BodyMatches(image + SyncAt, SyncEnd - SyncAt, 0x7e546384af3e3fc8ull))
        sync = Core::InstallGuestCodeHook(image + SyncAt, Entry, 0, OnSync, nullptr, true, &original_sync);
    replay_vtable = reinterpret_cast<Address>(image) + 0x53707d0;
    if (!bb_env_off("BB_REPLAY_STEP") &&
        BodyMatches(image + ReplayAt, ReplayEnd - ReplayAt, 0x65b4ccfbbc05261aull))
        replay = Core::InstallGuestCodeHook(image + ReplayAt, Entry, 0, OnReplay, nullptr, true, &original_replay);
    std::printf("Game-time cadence: replay %s (3 samples/s), character sync %s (6 packets/s default)\n",
                replay ? "on" : "off (disabled or body differs)", sync ? "on" : "off (disabled or body differs)");
}
} // namespace BbReplayTime
