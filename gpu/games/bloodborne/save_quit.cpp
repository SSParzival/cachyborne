// SPDX-License-Identifier: GPL-2.0-or-later
// Save-before-desktop utility from ayozetr/bloodborne_linux, with confirmation
// tied to the original affirmative functor instead of a pending global intent.
#include "games/bloodborne/save_quit.h"
#include "shim/bbport_guest_code_hooks.h"
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
extern "C" void runtime_quit_confirmed(void) __attribute__((weak));

namespace BbSaveQuit {
namespace {
constexpr std::uint64_t Factory=0x1c0c9f0, BuildFunctor=0x1c0ca4d, ConfirmFlag=0x1c0d0c4;
constexpr std::uint64_t Vtable=0x5344a10, Invoke=0x1c0d050;
constexpr std::array<unsigned char,18> FactoryEntry{
    0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x54,0x53,0x48,0x81,0xec,0xa0,0,0,0};
constexpr std::array<unsigned char,11> BuildEntry{
    0x4c,0x8d,0xbd,0x60,0xff,0xff,0xff,0x4c,0x89,0x7d,0x80};
constexpr std::array<unsigned char,10> ConfirmEntry{0xc7,0x80,0x50,0x02,0,0,0x01,0,0,0};
constexpr std::array<unsigned char,6> CopyMarker{0x8a,0x4b,0x08,0x88,0x48,0x08};
struct State {
    std::mutex mutex;
    unsigned char* image=nullptr;
    bool build_hook=false,confirm_hook=false;
    std::atomic<bool> available{false};
    Confirm confirmed=nullptr;
};
State& Get() { static auto* const state=new State;return *state; }
thread_local bool asking=false;
void __attribute__((sysv_abi)) MarkFunctor(std::uint64_t,const Core::GuestRegisterSnapshot* regs) {
    // The empty functor's byte +8 is copied by both retail clone functions and
    // ignored by its invocation. Initialize EVERY question: normal Quit Game
    // gets zero, our question one. Cancellation simply destroys this object.
    const unsigned char marker=asking ? 1 : 0;
    std::memcpy(reinterpret_cast<void*>(regs->rbp-0x98),&marker,1);
}
void __attribute__((sysv_abi)) ConfirmFunctor(std::uint64_t,const Core::GuestRegisterSnapshot* regs) {
    unsigned char marker=0;
    std::memcpy(&marker,reinterpret_cast<void*>(regs->rdi+8),1);
    if (marker==1) {
        // Do not re-arm if the same callback is invoked twice.
        const unsigned char clear=0;
        std::memcpy(reinterpret_cast<void*>(regs->rdi+8),&clear,1);
        Get().confirmed();
    }
}
}
bool Install(unsigned char* image,std::uint64_t size,Confirm confirmed) {
    if (!image || size<Vtable+0x18) return false;
    auto& s=Get();
    std::lock_guard lock{s.mutex};
    if (s.image && s.image!=image) return false;
    if (s.available.load(std::memory_order_acquire)) return true;
    Confirm callback=confirmed ? confirmed : runtime_quit_confirmed;
    std::uint64_t invoke=0;
    std::memcpy(&invoke,image+Vtable+0x10,8);
    if (!callback || invoke!=reinterpret_cast<std::uint64_t>(image)+Invoke ||
        std::memcmp(image+Factory,FactoryEntry.data(),FactoryEntry.size()) ||
        std::memcmp(image+0x1c0cffa,CopyMarker.data(),CopyMarker.size()) ||
        std::memcmp(image+0x1c0d03a,CopyMarker.data(),CopyMarker.size()) ||
        (!s.build_hook && std::memcmp(image+BuildFunctor,BuildEntry.data(),BuildEntry.size())) ||
        (!s.confirm_hook && std::memcmp(image+ConfirmFlag,ConfirmEntry.data(),ConfirmEntry.size()))) return false;
    s.image=image;s.confirmed=callback;
    if (!s.build_hook) {
        if (!Core::InstallGuestCodeHook(image+BuildFunctor,BuildEntry,0,MarkFunctor)) return false;
        s.build_hook=true;
    }
    if (!s.confirm_hook) {
        if (!Core::InstallGuestCodeHook(image+ConfirmFlag,ConfirmEntry,0,ConfirmFunctor)) return false;
        s.confirm_hook=true;
    }
    s.available.store(true,std::memory_order_release);
    std::puts("Save before desktop: original confirmation and save path; cancellation does not arm exit");
    return true;
}
bool Available() { return Get().available.load(std::memory_order_acquire); }
bool Asking() { return asking; }
void* OpenQuestion(void* out,void* arg) {
    if (!Available()) return nullptr;
    struct Scope {
        bool previous=asking;
        Scope() { asking=true; }
        ~Scope() { asking=previous; }
    } scope;
    using Fn=void*(__attribute__((sysv_abi)) *)(void*,void*);
    return reinterpret_cast<Fn>(Get().image+Factory)(out,arg);
}
}
