// SPDX-License-Identifier: GPL-3.0-or-later
// Copied-save-only native combat fixture. Grants the actual weapon through
// retail inventory functions and meets its requirements through ApplyStats.
// Never applies the tested aura: special effects are observed read-only.
// SpEffect layout/call adapters: BBHOST engine/frame_rate.cpp, player_data.cpp
// and host/plugins.cpp @ bdc5cdf8be5f37188da9c2368e92be444fdc04cb, GPL-2.0+.
#include "games/bloodborne/player_data.h"
#include "common/guest_memory.h"
#include "shim/bbport_frame_tasks.h"
#include <array>
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
Address base=0;
std::filesystem::path marker;
std::atomic<pid_t> owner{0};
bool granted=false;
std::uint64_t checked=0,reported=0,seen=0;
std::array<bool,2> previous{};
Address Pointer(Address at) { Address p=0;BbGuestMemory::Read(at,p);return p; }
void Tick() {
    const auto tid=gettid();pid_t zero=0;owner.compare_exchange_strong(zero,tid);
    if (owner.load()!=tid) return;
    const auto global=Pointer(base+0x5556678);
    std::uint8_t loading=1;
    if (!global || !BbGuestMemory::Read(global+8,loading) || loading) return;
    const auto world=Pointer(base+0x553e878),chr=world?Pointer(world+0x60):0;
    std::uint32_t block=0;
    if (!chr || !BbGuestMemory::Read(chr+0x3f8,block) || !block || block==0xffffffff) return;
    const auto now=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    if (now-checked<50'000'000ull) return;
    checked=now;
    if (granted) {
        std::array<bool,2> found{};
        const auto effects=Pointer(chr+0x1c8);
        auto node=effects?Pointer(effects+8):0;
        unsigned visited=0;
        for (;node && visited<128;++visited) {
            std::int32_t id=0;
            if (!BbGuestMemory::Read(node+0x40,id)) break;
            if (id==5286) found[0]=true;
            if (id==5296) found[1]=true;
            const auto next=Pointer(node+0x58);
            if (next==node) break;
            node=next;
        }
        if (found!=previous || now-reported>=5'000'000'000ull) {
            reported=now;
            std::printf("Bloodletter fixture effects: ns=%llu aura5286=%d tick5296=%d nodes=%u bounded=%d\n",
                (unsigned long long)now,found[0],found[1],visited,node!=0);
            std::fflush(stdout);previous=found;
        }
    }
    std::ifstream file(marker.parent_path()/"fixture-command");
    std::uint64_t sequence=0;std::string command;
    if (!(file>>sequence>>command) || sequence<=seen) return;
    seen=sequence;
    if (command=="watch" && !granted) {
        granted=true;
        std::puts("Bloodletter fixture watch: read-only effects; no inventory or stat changes");
        std::fflush(stdout);return;
    }
    if (command!="arm" || granted || block!=0x15000000) return; // Hunter's Dream
    const auto p=BbPlayerData::Read({nullptr,BbGuestMemory::Read},base+0x553b130);
    if (!p || p->build.level<1 || p->build.level>537) return;
    alignas(16) std::array<std::uint32_t,10> attributes{};
    int delta=0;
    for (unsigned i=0;i<6;++i) {
        if (p->build.stats[i]<1 || p->build.stats[i]>99) return;
        auto value=p->build.stats[i];
        if (i==2 && value<14) value=14;
        if (i==4 && value<16) value=16;
        delta+=value-p->build.stats[i];attributes[3+i]=value;
    }
    attributes[2]=p->build.level+delta;
    const auto inventory=p->record+0x328;
    alignas(16) std::uint32_t code=29000000;
    using Fits=std::int32_t(*)(Address,const std::uint32_t*,std::int32_t);
    const auto fits=reinterpret_cast<Fits>(base+0x14d9a80)(inventory,&code,1);
    if (fits<=0) return;
    // EquipGameData::Add constructs a weapon instance. The lower-level
    // inventory add expects an existing instance handle, not a raw weapon ID.
    using Add=std::uint64_t(*)(Address,std::uint32_t,std::uint32_t,std::int32_t,
                              std::int32_t,std::int32_t,std::int32_t);
    const auto result=reinterpret_cast<Add>(base+0x14cdfd0)(p->record+0x1d0,0,29000000,1,1,1,0);
    using Apply=std::int64_t(*)(void*);
    reinterpret_cast<Apply>(base+0x1c13d00)(attributes.data());
    granted=true;
    std::printf("Bloodletter fixture armed: weapon=29000000 add=%llu level=%u strength=%u bloodtinge=%u; aura never injected\n",
        (unsigned long long)result,attributes[2],attributes[5],attributes[7]);
    std::fflush(stdout);
}
}

extern "C" void bbgpu_patch_image(unsigned char* image,std::uint64_t size) {
    using Patch=void(*)(unsigned char*,std::uint64_t);
    auto original=reinterpret_cast<Patch>(dlsym(RTLD_NEXT,"bbgpu_patch_image"));
    if (!original) std::abort();original(image,size);
    const auto guard=std::getenv("CACHYBORNE_BLOODLETTER_UI_FIXTURE");
    const auto user=std::getenv("BB_GPU_USER_DIR");
    for (const auto name:{"BB_ONLINE","BB_BBSC"})
        if (const auto value=std::getenv(name);value && value[0]=='1') std::abort();
    if (!guard || !user) std::abort();marker=guard;
    std::ifstream file(marker);std::string magic((std::istreambuf_iterator<char>(file)),{});
    if (magic!="cachyborne-copied-save-only\n" || marker.filename()!=".copied-save-fixture" ||
        std::filesystem::path(user).parent_path()!=marker.parent_path() ||
        marker.parent_path().filename().string().find("bloodletter-ui-owned-")!=0) std::abort();
    constexpr unsigned char inventory_entry[]{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,
        0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec};
    constexpr unsigned char add_entry[]{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,
        0x41,0x55,0x41,0x54,0x53,0x48,0x81,0xec,0x18,0x01,0x00,0x00};
    constexpr unsigned char apply_entry[]{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,
        0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec,0x28,0x4c,0x8b,0x35};
    if (size<0x5556680 || std::memcmp(image+0x14d9a80,inventory_entry,sizeof inventory_entry) ||
        std::memcmp(image+0x14cdfd0,add_entry,sizeof add_entry) ||
        std::memcmp(image+0x1c13d00,apply_entry,sizeof apply_entry)) std::abort();
    base=reinterpret_cast<Address>(image);
    if (!BbFrameTasks::Subscribe(image,size,Tick)) std::abort();
    std::puts("Bloodletter fixture: guarded copied-save inventory/stat setup and read-only aura observer armed");
    std::fflush(stdout);
}
