// SPDX-License-Identifier: GPL-3.0-or-later
// Copied-save-only water diagnostics and exact in-map lake placement. This
// fixture is never loaded by a normal launch and does not ship as a feature.
// Warp call/layout adapted from BBHOST engine/world_chr.cpp, GPL-3.0-or-later.
#include "games/bloodborne/water_gate.h"
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
Address base=0;
std::filesystem::path marker;
std::atomic<pid_t> owner{0};
std::uint64_t checked=0,reported=0,seen=0;
Address Pointer(Address at) { Address v=0; BbGuestMemory::Read(at,v); return v; }
void Tick() {
    const pid_t tid=gettid(); pid_t zero=0; owner.compare_exchange_strong(zero,tid);
    if (owner.load()!=tid) return;
    const auto now=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    if (now-reported>=5'000'000'000ull) {
        reported=now;
        const auto s=BbWaterGate::Read();
        std::printf("Water fixture stats: %llu %llu %llu %llu %llu %llu %llu %llu\n",
            (unsigned long long)s.frames,(unsigned long long)s.steps,(unsigned long long)s.camera_steps,
            (unsigned long long)s.held,(unsigned long long)s.grids_restored,
            (unsigned long long)s.impulses_kept,(unsigned long long)s.impulses_replaced,
            (unsigned long long)s.impulses_lost);
        std::fflush(stdout);
    }
    if (now-checked<100'000'000ull) return;
    checked=now;
    std::ifstream request(marker.parent_path()/"fixture-command");
    std::uint64_t seq=0; std::string action;
    if (!(request>>seq>>action) || seq<=seen) return;
    const auto state=Pointer(base+0x5556678);
    std::uint8_t loading=1;
    if (!state || !BbGuestMemory::Read(state+8,loading) || loading) return;
    const auto world=Pointer(base+0x553e878), player=world?Pointer(world+0x60):0;
    std::uint32_t block=0;
    if (!player || !BbGuestMemory::Read(player+0x3f8,block) || block!=0x20000000) return;
    seen=seq;
    if (action!="lake") return;
    const auto res=Pointer(base+0x553b148);
    if (!res) return;
    alignas(16) std::int32_t blocks[4]={std::int32_t(block),0,0,0};
    alignas(16) float pos[4]={-508.0f,-270.5f,378.0f,0.0f};
    alignas(16) float rot[4]={0.0f,1.0210176f,0.0f,0.0f};
    using Warp=std::int64_t(*)(Address,const std::int32_t*,const float*,const float*);
    const auto result=reinterpret_cast<Warp>(base+0x154b110)(res,blocks,pos,rot);
    std::printf("Water fixture: %llu lake placement requested through retail warp, result=%lld\n",
                (unsigned long long)seq,(long long)result);
    std::fflush(stdout);
}
}

extern "C" void bbgpu_patch_image(unsigned char* image,std::uint64_t size) {
    using Patch=void(*)(unsigned char*,std::uint64_t);
    auto original=reinterpret_cast<Patch>(dlsym(RTLD_NEXT,"bbgpu_patch_image"));
    if (!original) std::abort();
    original(image,size);
    const char* guard=std::getenv("CACHYBORNE_WATER_UI_FIXTURE");
    const char* user=std::getenv("BB_GPU_USER_DIR");
    const char* online=std::getenv("BB_ONLINE");
    const char* bbsc=std::getenv("BB_BBSC");
    const char* feature=std::getenv("BB_WATER_GATE");
    if (!guard || !user || !feature || feature[0]!='1' ||
        (online && online[0]=='1') || (bbsc && bbsc[0]=='1')) std::abort();
    marker=guard;
    std::ifstream file(marker); std::string magic((std::istreambuf_iterator<char>(file)),{});
    if (magic!="cachyborne-copied-save-only\n" || marker.filename()!=".copied-save-fixture" ||
        std::filesystem::path(user).parent_path()!=marker.parent_path() ||
        marker.parent_path().filename().string().find("water-ui-owned-")!=0) std::abort();
    constexpr unsigned char entry[]{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,
        0x41,0x54,0x53,0x48,0x83,0xec,0x28,0x49,0x89,0xcf};
    if (size<0x5556680 || std::memcmp(image+0x154b110,entry,sizeof entry)) std::abort();
    base=reinterpret_cast<Address>(image);
    if (!BbFrameTasks::Subscribe(image,size,Tick)) std::abort();
    std::puts("Water fixture: guarded copied-save diagnostics armed"); std::fflush(stdout);
}
