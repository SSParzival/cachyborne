// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_frame_tasks.h"
#include "bbport_guest_code_hooks.h"
#include <array>
#include <atomic>
#include <mutex>

namespace BbFrameTasks {
namespace {
constexpr std::uint64_t Pacer = 0x2034770;
constexpr std::array<unsigned char,6> Entry{0x55,0x48,0x89,0xe5,0x41,0x57};
struct State {
    std::mutex mutex;
    unsigned char* image = nullptr;
    std::array<Task,16> tasks{};
    std::atomic<unsigned> count{0};
};
State& Get() {
    // Guest callbacks can still execute during host exit callbacks.
    static auto* const state = new State;
    return *state;
}
void __attribute__((sysv_abi)) OnFrame(std::uint64_t,const Core::GuestRegisterSnapshot*) {
    auto& state = Get();
    const auto count = state.count.load(std::memory_order_acquire);
    for (unsigned i=0;i<count;++i) state.tasks[i]();
}
}
bool Subscribe(unsigned char* image,std::uint64_t size,Task task) {
    if (!image || size<Pacer+Entry.size() || !task) return false;
    auto& state = Get();
    std::lock_guard lock{state.mutex};
    const auto count = state.count.load(std::memory_order_relaxed);
    if (state.image && state.image!=image) return false;
    for (unsigned i=0;i<count;++i) if (state.tasks[i]==task) return true;
    if (count==state.tasks.size()) return false;
    if (!state.image) {
        if (!Core::InstallGuestCodeHook(image+Pacer,Entry,0,OnFrame)) return false;
        state.image=image;
    }
    state.tasks[count]=task;
    state.count.store(count+1,std::memory_order_release);
    return true;
}
}
