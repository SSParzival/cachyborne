// SPDX-License-Identifier: GPL-3.0-or-later
#include "games/bloodborne/menu_steps.h"
#include "shim/bbport_guest_code_hooks.h"
#include <array>
#include <atomic>
#include <cstring>
#include <limits>
#include <mutex>

namespace BbMenuSteps {
namespace {
constexpr std::uint64_t Constructor = 0x1c1cce0;
constexpr std::array<unsigned char,16> Entry{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,
    0x53,0x50,0x4d,0x89,0xce,0x48,0x89,0xfb};
struct WatchSlot {
    std::uint64_t type=0;
    const char16_t* name=nullptr;
    std::atomic<std::uintptr_t> made{0};
};
struct State {
    std::mutex mutex;
    unsigned char* image=nullptr;
    std::array<WatchSlot,8> watches;
    std::atomic<unsigned> count{0};
};
State& Get() { static auto* const state = new State; return *state; }
bool NameEquals(const char16_t* a,const char16_t* b) {
    if (!a || !b) return false;
    for (unsigned i=0;i<128;++i) {
        if (a[i]!=b[i]) return false;
        if (!a[i]) return true;
    }
    return false;
}
void __attribute__((sysv_abi)) OnStep(std::uint64_t,const Core::GuestRegisterSnapshot* regs) {
    const auto* name=reinterpret_cast<const char16_t*>(regs->rcx);
    if (!name) return;
    auto& state=Get();
    const auto count=state.count.load(std::memory_order_acquire);
    for (unsigned i=0;i<count;++i) {
        auto& watch=state.watches[i];
        if (watch.type==regs->rdx && NameEquals(watch.name,name))
            watch.made.store(regs->rdi,std::memory_order_release);
    }
}
std::atomic_ref<std::int32_t> References(std::uintptr_t step) {
    return std::atomic_ref<std::int32_t>(*reinterpret_cast<std::int32_t*>(step+8));
}
}
int Watch(unsigned char* image,std::uint64_t size,std::uint64_t type,const char16_t* name) {
    if (!image || size<Constructor+Entry.size() || !name || !*name) return -1;
    // Bound the identity before publishing it to the constructor observer.
    unsigned length=0;
    while (length<128 && name[length]) ++length;
    if (length==128) return -1;
    auto& state=Get();
    std::lock_guard lock{state.mutex};
    if (state.image && state.image!=image) return -1;
    const auto count=state.count.load(std::memory_order_relaxed);
    for (unsigned i=0;i<count;++i)
        if (state.watches[i].type==type && NameEquals(state.watches[i].name,name)) return -1;
    if (count==state.watches.size()) return -1;
    if (!state.image) {
        if (!Core::InstallGuestCodeHook(image+Constructor,Entry,0,OnStep)) return -1;
        state.image=image;
    }
    auto& watch=state.watches[count];
    watch.type=type;watch.name=name;
    state.count.store(count+1,std::memory_order_release);
    return int(count);
}
std::uintptr_t Take(int watch) {
    auto& state=Get();
    if (watch<0 || unsigned(watch)>=state.count.load(std::memory_order_acquire)) return 0;
    return state.watches[watch].made.exchange(0,std::memory_order_acq_rel);
}
std::int32_t Count(std::uintptr_t step) {
    return step ? References(step).load(std::memory_order_acquire) : 0;
}
bool Hold(std::uintptr_t step) {
    if (!step) return false;
    auto refs=References(step);
    auto count=refs.load(std::memory_order_relaxed);
    while (count>0 && count<std::numeric_limits<std::int32_t>::max())
        if (refs.compare_exchange_weak(count,count+1,std::memory_order_acq_rel)) return true;
    return false;
}
bool Release(std::uintptr_t step) {
    if (!step) return false;
    auto refs=References(step);
    auto count=refs.load(std::memory_order_relaxed);
    while (count>0) {
        if (!refs.compare_exchange_weak(count,count-1,std::memory_order_acq_rel)) continue;
        if (count==1) {
            std::uintptr_t vtable, destroy;
            std::memcpy(&vtable,reinterpret_cast<void*>(step),sizeof vtable);
            std::memcpy(&destroy,reinterpret_cast<void*>(vtable),sizeof destroy);
            reinterpret_cast<void(__attribute__((sysv_abi)) *)(std::uintptr_t)>(destroy)(step);
        }
        return true;
    }
    return false;
}
}
