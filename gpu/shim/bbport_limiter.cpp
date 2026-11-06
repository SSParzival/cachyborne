// SPDX-License-Identifier: GPL-3.0-or-later
#include "bbport_limiter.h"
#include "bbport_guest_code.h"
#include "bbport_guest_code_hooks.h"
#include "bbport_map_near.h"
#include "common/types.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <new>
#include <utility>
#include <sys/mman.h>

namespace BbLimiter {
namespace {
using Sleep=int(PS4_SYSV_ABI *)(std::uint32_t);
struct Shared {
    float positive,negative;
    Margin margin;
    Sleep original;
    Shared(int fixed,Sleep sleep) : positive{float(fixed<0 ? 2000 : fixed)*1e-6f},
        negative{-positive},margin{fixed},original{sleep} {}
};
static_assert(sizeof(Shared)<4096);
Shared* shared=nullptr; // process lifetime, used by the game's main thread
int PS4_SYSV_ABI SleepWithMargin(std::uint32_t us) {
    const auto start=std::chrono::steady_clock::now();
    const int result=shared->original(us);
    const auto elapsed=std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now()-start).count();
    shared->margin.Observe(elapsed>us ? elapsed-us : 0);
    shared->positive=float(shared->margin.Microseconds())*1e-6f;
    shared->negative=-shared->positive;
    if (shared->margin.Samples()==32)
        std::printf("Adaptive limiter: margin %d us after 32 sleeps\n",shared->margin.Microseconds());
    return result;
}
struct Patch {
    std::uint32_t at;
    std::array<unsigned char,8> bytes;
    unsigned size;
};
constexpr std::array<Patch,5> Expected{{
    {0x2034ada,{0x0f,0x84,0x9a,0,0,0},6},
    {0x2034b9e,{0xc5,0xf8,0x2e,0x05,0xea,0x47,0x8f,0x02},8},
    {0x2034ba8,{0xc5,0xf8,0x2e,0x05,0xe4,0x47,0x8f,0x02},8},
    {0x2034bb2,{0xc5,0xfa,0x58,0x05,0xde,0x47,0x8f,0x02},8},
    {0x2034bd2,{0xe8,0x91,0xb2,0xb8,0x00},5},
}};
}

bool Install(unsigned char* image,std::uint64_t size,int fixed_us) {
    if (!image || shared || fixed_us < -1 || fixed_us>5000 || size<0x492939c) return false;
    for (const auto& patch:Expected)
        if (std::memcmp(image+patch.at,patch.bytes.data(),patch.size)) return false;
    for (const auto& [at,bits]:std::array<std::pair<std::uint32_t,std::uint32_t>,3>{{
             {0x4929390,0x3a83126f},{0x4929394,0x3ba3d70a},{0x4929398,0xbba3d70a}}}) {
        std::uint32_t actual=0;std::memcpy(&actual,image+at,4);
        if (actual!=bits) return false;
    }
    const auto base=reinterpret_cast<std::uintptr_t>(image);
    auto* memory=static_cast<unsigned char*>(BbMapNear::Map(base,base+size,8192,
        PROT_READ|PROT_WRITE,8u<<20,"Adaptive limiter"));
    if (!memory) return false;
    auto* candidate=new(memory) Shared(fixed_us,reinterpret_cast<Sleep>(base+0x2bbfe68));
    auto* code=memory+4096;
    BbGuestCode::Emitter stub;stub.JmpAbs(reinterpret_cast<std::uintptr_t>(&SleepWithMargin));
    std::memcpy(code,stub.code.data(),stub.size);
    auto patches=Expected;
    patches[0].bytes={0xe9,0x9b,0,0,0,0x90}; // same sleep path for foreground too
    for (unsigned i=1;i<=3;++i) {
        const auto to=reinterpret_cast<std::uintptr_t>(i==3 ? &candidate->negative : &candidate->positive);
        const auto delta=std::int64_t(to)-std::int64_t(base+patches[i].at+8);
        if (delta!=std::int32_t(delta)) { munmap(memory,8192);return false; }
        const auto rel=std::int32_t(delta);std::memcpy(patches[i].bytes.data()+4,&rel,4);
    }
    const auto delta=std::int64_t(reinterpret_cast<std::uintptr_t>(code))-
                     std::int64_t(base+patches[4].at+5);
    if (delta!=std::int32_t(delta) || mprotect(code,4096,PROT_READ|PROT_EXEC)) {
        munmap(memory,8192);return false;
    }
    const auto rel=std::int32_t(delta);std::memcpy(patches[4].bytes.data()+1,&rel,4);
    auto* page=reinterpret_cast<void*>((base+Expected.front().at)&~std::uintptr_t(4095));
    if (mprotect(page,4096,PROT_READ|PROT_WRITE|PROT_EXEC)) { munmap(memory,8192);return false; }
    shared=candidate; // published before the call site, before guest startup
    for (const auto& patch:patches) std::memcpy(image+patch.at,patch.bytes.data(),patch.size);
    if (mprotect(page,4096,Core::GuestCodeProtection())) {
        for (const auto& patch:Expected) std::memcpy(image+patch.at,patch.bytes.data(),patch.size);
        shared=nullptr;munmap(memory,8192);
        std::fputs("Adaptive limiter: could not restore guest code protection\n",stderr);
        return false;
    }
    return true;
}
}
