// SPDX-License-Identifier: GPL-3.0-or-later
// BBHOST PR #104: EventProjectI does not initialize its language count before
// fallible bank loading. Preserve the whole constructor, zeroing only its count.
#include "games/bloodborne/fmod_project_init.h"
#include "shim/bbport_guest_code_hooks.h"
#include "shim/bb_env.h"
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace BbFmodProjectInit {
namespace {
constexpr std::uint64_t Constructor = 0xdf3ce0; // retail 1.09 BN 0x11f3ce0
constexpr std::array<unsigned char,14> Entry{
    0x55,0x48,0x89,0xe5,0x41,0x56,0x53,0x48,0x89,0xfb,0x48,0x8d,0x43,0x08};
struct State {
    std::mutex mutex;
    unsigned char* image=nullptr;
    std::atomic<bool> enabled{false};
    std::atomic<std::uint64_t> initialized{0};
};
State& Get() { static auto* const state=new State;return *state; }
void __attribute__((sysv_abi)) Initialize(std::uint64_t,const Core::GuestRegisterSnapshot* regs) {
    // The original constructor requires exclusively owned 0x288-byte storage.
    // Its LANG loader can later overwrite this with the real language count.
    constexpr std::uint32_t zero=0;
    std::memcpy(reinterpret_cast<void*>(regs->rdi+0x170),&zero,sizeof zero);
    const auto number=Get().initialized.fetch_add(1,std::memory_order_relaxed)+1;
    if (number<=4)
        std::printf("FMOD project initialization: initialized project %llu language count\n",
                    static_cast<unsigned long long>(number));
}
}
bool Install(unsigned char* image,std::uint64_t size) {
    const auto* setting=bb_env("BB_FMOD_PROJECT_INIT");
    if (!setting || std::strcmp(setting,"1")) return true;
    if (!image || size<Constructor+Entry.size()) return false;
    auto& state=Get();
    std::lock_guard lock{state.mutex};
    if (state.image) return state.image==image;
    if (!Core::InstallGuestCodeHook(image+Constructor,Entry,0,Initialize)) return false;
    state.image=image;
    state.enabled.store(true,std::memory_order_release);
    std::puts("FMOD project initialization: language count starts at zero");
    return true;
}
Stats Read() {
    auto& state=Get();
    return {state.enabled.load(std::memory_order_acquire),state.initialized.load(std::memory_order_relaxed)};
}
}
