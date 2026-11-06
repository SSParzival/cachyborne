// SPDX-License-Identifier: GPL-2.0-or-later
// Explicit copied-save UI fixture, never loaded by a normal launch.
// Marks the local Ebrietas event complete before entering its map. A separate
// fixture command grants exactly one Yharnam Stone for the positive UI branch.
#include "games/bloodborne/event_flags.h"
#include "common/guest_memory.h"
#include "shim/bbport_frame_tasks.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
namespace {
using Address=std::uintptr_t;
Address base=0;std::filesystem::path marker;
std::atomic<pid_t> owner{0};bool boss_ready=false,stone_added=false;
std::uint64_t last_check=0,seen=0;
Address Pointer(Address at) { Address value=0;BbGuestMemory::Read(at,value);return value; }
void Tick() {
    const pid_t tid=gettid();pid_t zero=0;owner.compare_exchange_strong(zero,tid);
    if (owner.load()!=tid) return;
    const auto global=Pointer(base+0x5556678);
    std::uint8_t loading=1;
    if (!global || !BbGuestMemory::Read(global+8,loading) || loading) return;
    const auto manager=Pointer(base+0x553e878),player=manager ? Pointer(manager+0x60) : 0;
    std::uint32_t block=0;
    if (!player || !BbGuestMemory::Read(player+0x3f8,block) || !block || block==0xffffffff) return;
    if (!boss_ready) {
        const std::uint32_t flag=12421800;bool value=false;
        if (BbEventFlags::Set(base,flag,true) && BbEventFlags::Read(base,std::span(&flag,1),std::span(&value,1)) && value) {
            boss_ready=true;
            std::printf("Rebirth UI fixture: event 12421800 marked complete on copied save, before target-map load (block 0x%08x)\n",block);
            std::fflush(stdout);
        }
        return;
    }
    const auto now=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (std::uint64_t(now)-last_check<100000000) return;last_check=now;
    std::ifstream request(marker.parent_path()/"fixture-command");std::uint64_t seq=0;std::string action;
    if (!(request>>seq>>action) || seq<=seen) return;seen=seq;
    if (action!="stone" || stone_added || block!=0x18020000) {
        std::printf("Rebirth UI fixture: request %llu refused\n",(unsigned long long)seq);std::fflush(stdout);return;
    }
    const auto data_manager=Pointer(base+0x553b130),record=data_manager ? Pointer(data_manager+8) : 0;
    if (!record) return;
    const Address inventory=record+0x328;
    alignas(16) std::uint32_t code=0x40000000u|4330,key=0x80000000u|(3u<<28)|4330;
    using Fits=std::int32_t(*)(Address,const std::uint32_t*,std::int32_t);
    using Add=std::int32_t(*)(Address,const std::uint32_t*,std::int32_t);
    const auto fits=reinterpret_cast<Fits>(base+0x14d9a80)(inventory,&code,1);
    if (fits<=0) { std::printf("Rebirth UI fixture: stone request fits=%d, not added\n",fits);std::fflush(stdout);return; }
    const auto result=reinterpret_cast<Add>(base+0x14da0a0)(inventory,&key,1);
    stone_added=true;std::printf("Rebirth UI fixture: one stone added through retail inventory, result=%d\n",result);std::fflush(stdout);
}
}
extern "C" void bbgpu_patch_image(unsigned char* image,std::uint64_t size) {
    using Patch=void(*)(unsigned char*,std::uint64_t);
    auto original=reinterpret_cast<Patch>(dlsym(RTLD_NEXT,"bbgpu_patch_image"));if (!original) std::abort();
    original(image,size);
    const char* guard=std::getenv("CACHYBORNE_REBIRTH_UI_FIXTURE");
    const char* user=std::getenv("BB_GPU_USER_DIR");
    if (!guard || !user) std::abort();marker=guard;
    std::ifstream file(marker);std::string magic((std::istreambuf_iterator<char>(file)),{});
    if (magic!="cachyborne-copied-save-only\n" || marker.filename()!=".copied-save-fixture" ||
        std::filesystem::path(user).parent_path()!=marker.parent_path() ||
        marker.parent_path().filename().string().find("rebirth-ui-owned-")!=0 || !BbEventFlags::Validate(image,size)) std::abort();
    constexpr unsigned char entry[]{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec};
    if (size<0x553b138 || std::memcmp(image+0x14d9a80,entry,sizeof entry) || std::memcmp(image+0x14da0a0,entry,sizeof entry)) std::abort();
    base=reinterpret_cast<Address>(image);if (!BbFrameTasks::Subscribe(image,size,Tick)) std::abort();
    std::puts("Rebirth UI fixture: guarded copied-save conditions armed");std::fflush(stdout);
}
