// SPDX-License-Identifier: GPL-3.0-or-later
// Adapted from bbhost v0.2.24 engine/change_appearance.cpp; generated assets
// belong to scripts/world_assets.py, while this file owns only guest UI state.
#include "games/bloodborne/appearance.h"
#include "games/bloodborne/menu_steps.h"
#include "shim/bbport_frame_tasks.h"
#include "shim/bb_env.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <sys/uio.h>
#include <unistd.h>

namespace BbAppearance {
namespace {
using Address=std::uintptr_t;
constexpr Address OpenWindow=0x1ad8150,CloseWindow=0x1ad8e60,MenuHeap=0x5540418;
constexpr Address DescVtable=0x5336b30,OwnerVtable=0x5336b70,ListVtable=0x53310d0;
constexpr Address FactoryVtable=0x533ee10,FrameFactory=0x1b08010,FrameName=0x4998486;
constexpr char16_t ListName[]=u"ChrMakeCommandList";
constexpr char16_t WindowName[]=u"ChrMake_BG";
struct State {
    Address base=0,list=0,descriptor=0;
    int watch=-1;
    unsigned char active=1;
    std::atomic<pid_t> owner{0};
    std::atomic<bool> enabled{false};
    std::atomic<std::uint64_t> opened{0},closed{0},updates{0},failed{0};
};
State& Get() { static auto* const state=new State;return *state; }
template<class T> bool ReadValue(Address at,T& value) {
    if (at<0x10000 || at>UINTPTR_MAX-sizeof(T)) return false;
    iovec local{&value,sizeof value},remote{reinterpret_cast<void*>(at),sizeof value};
    return process_vm_readv(getpid(),&local,1,&remote,1,0)==ssize_t(sizeof value);
}
Address Pointer(Address at) { Address value=0;ReadValue(at,value);return value; }
template<class T> void Store(Address at,T value) { std::memcpy(reinterpret_cast<void*>(at),&value,sizeof value); }
template<class... Args> std::uint64_t Call(Address fn,Args... args) {
    return reinterpret_cast<std::uint64_t(__attribute__((sysv_abi)) *)(Args...)>(fn)(args...);
}
bool InWorld(const State& state) {
    const auto man=Pointer(state.base+0x553e878);
    const auto player=man ? Pointer(man+0x60) : 0;
    std::uint32_t block=0xffffffff;
    return player && ReadValue(player+0x3f8,block) && block!=0xffffffff;
}
bool LiveList(Address list) {
    std::uint64_t type=0;
    std::int32_t count=0;
    if (!list || !ReadValue(list+8,count) || count<=0 ||
        !ReadValue(list+0x28,type) || type!=7) return false;
    const auto name=Pointer(list+0x30);
    char16_t text[sizeof ListName/sizeof(char16_t)]{};
    iovec local{text,sizeof text},remote{reinterpret_cast<void*>(name),sizeof text};
    return name && process_vm_readv(getpid(),&local,1,&remote,1,0)==ssize_t(sizeof text) &&
        !std::memcmp(text,ListName,sizeof text);
}
Address Allocate(State& state) {
    const auto heap=Pointer(state.base+MenuHeap);
    const auto vtable=heap ? Pointer(heap) : 0;
    const auto allocate=vtable ? Pointer(vtable+0x58) : 0;
    return allocate ? Call(allocate,heap,std::uint64_t(0xc0),std::uint64_t(0x10)) : 0;
}
void Free(State& state,Address descriptor) {
    const auto heap=Pointer(state.base+MenuHeap);
    const auto vtable=heap ? Pointer(heap) : 0;
    const auto free=vtable ? Pointer(vtable+0x70) : 0;
    if (free) Call(free,heap,descriptor);
}
bool Open(State& state) {
    const auto d=Allocate(state);
    if (!d) return false;
    std::memset(reinterpret_cast<void*>(d),0,0xc0);
    Store(d,state.base+DescVtable);Store(d+8,state.base+OwnerVtable);
    Store(d+0x18,std::uint64_t(6));Store(d+0x20,state.base+FrameName);
    Store(d+0x30,state.base+FactoryVtable);Store(d+0x38,state.base+FrameFactory);
    Store(d+0x50,d+0x30);Store(d+0x68,state.base+ListVtable);
    Call(state.base+OpenWindow,d);
    if (!Pointer(d+0x60)) {
        Call(state.base+CloseWindow,d);Free(state,d);
        return false;
    }
    state.descriptor=d;
    ++state.opened;
    std::puts("Appearance: opened ChrMake_BG preview around the mirror's appearance list");
    return true;
}
void Close(State& state) {
    if (state.descriptor) {
        Call(state.base+CloseWindow,state.descriptor);Free(state,state.descriptor);
        state.descriptor=0;
        ++state.closed;
    }
    if (state.list) {
        BbMenuSteps::Release(state.list);
        state.list=0;
    }
}
void Tick() {
    auto& state=Get();
    if (!state.enabled.load(std::memory_order_acquire)) return;
    const auto tid=gettid();
    pid_t none=0;state.owner.compare_exchange_strong(none,tid);
    if (state.owner.load()!=tid) return;
    const auto made=BbMenuSteps::Take(state.watch);
    if (!made && !state.list && !state.descriptor) return;
    const bool world=InWorld(state);
    // Close an older menu before considering a replacement from this frame.
    // A feature-owned reference must not keep its preview open after the
    // game's menu has released its last reference, or across world teardown.
    if (state.list && (!world || BbMenuSteps::Count(state.list)<=1)) Close(state);
    if (made && !state.list && world && LiveList(made) && BbMenuSteps::Hold(made)) {
        // Hold before opening another window: allocations/callbacks in the
        // guest window factory cannot invalidate the step we are wrapping.
        if (Open(state)) state.list=made;
        else { BbMenuSteps::Release(made);++state.failed; }
    }
    if (state.descriptor) {
        const auto controller=Pointer(state.descriptor+0x60);
        const auto vtable=controller ? Pointer(controller) : 0;
        const auto update=vtable ? Pointer(vtable+0x18) : 0;
        if (update) {
            Call(update,controller,reinterpret_cast<Address>(&state.active));
            ++state.updates;
        } else Close(state);
    }
    if (state.list && BbMenuSteps::Count(state.list)==1) Close(state);
}
}
bool Install(unsigned char* image,std::uint64_t size) {
    if (!bb_env_on("BB_CHANGE_APPEARANCE")) return false;
    auto& state=Get();
    if (state.enabled.load(std::memory_order_acquire)) return state.base==reinterpret_cast<Address>(image);
    constexpr unsigned char open[]{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec,0x18};
    constexpr unsigned char close[]{0x55,0x48,0x89,0xe5,0x53,0x50,0x48,0x89,0xfb,0x48,0x8d,0x05,0xc0,0xdc,0x85,0x03};
    constexpr unsigned char factory[]{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x54,0x53,0x48,0x83,0xec,0x70};
    if (!image || size<MenuHeap+8 || std::memcmp(image+OpenWindow,open,sizeof open) ||
        std::memcmp(image+CloseWindow,close,sizeof close) ||
        std::memcmp(image+FrameFactory,factory,sizeof factory) ||
        std::memcmp(image+FrameName,WindowName,sizeof WindowName)) return false;
    // A conflicting pacer must not consume a watch: a corrected retry would
    // otherwise be refused as a duplicate even though this feature never ran.
    if (!BbFrameTasks::Subscribe(image,size,Tick)) return false;
    const int watch=BbMenuSteps::Watch(image,size,7,ListName);
    if (watch<0) return false;
    state.base=reinterpret_cast<Address>(image);state.watch=watch;
    state.enabled.store(true,std::memory_order_release);
    std::puts("Appearance: mirror preview controller enabled (restored layout required)");
    return true;
}
Stats Read() {
    auto& state=Get();
    return {state.opened.load(),state.closed.load(),state.updates.load(),state.failed.load(),state.enabled.load()};
}
}
