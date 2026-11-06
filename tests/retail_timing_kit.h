// SPDX-License-Identifier: GPL-3.0-or-later
// Small test-only loader for the original 1.09 timing functions. Applies base
// relocations and binds only the canary; tests stub the functions they reach.
#pragma once
#include "games/bloodborne/replay_time.h"
#include <algorithm>
#include <cassert>
#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <random>
#include <vector>
#include <elf.h>
#include <sys/mman.h>
#define GUEST_ABI
using u8=std::uint8_t;
using u32=std::uint32_t;
using u64=std::uint64_t;
using s8=std::int8_t;
using s32=std::int32_t;
using s64=std::int64_t;
using ull=unsigned long long;
using BbReplayTime::Load;
template<class T> T load(u64 at) { return Load<T>(at); }
template<class T> void store(u64 at,T value) { BbReplayTime::Store(at,value); }
namespace frame_clock {
inline constexpr float kThirtieth=BbFrameRate::Thirtieth;
inline auto clock=std::make_unique<BbFrameRate::FrameClock>();
inline BbFrameRate::ClockFrame current;
inline void reset() { clock=std::make_unique<BbFrameRate::FrameClock>(); }
inline void publish(float dt) { clock->Publish(dt); current=clock->Read(); }
inline float frames30_of(float dt) { return BbFrameRate::ClockFrame{dt}.Frames30(); }
inline float frames30() { return current.Frames30(); }
}
namespace eboot_kit {
inline u8* image;
inline u8* at(u64 bn) { return reinterpret_cast<u8*>(bn); }
template<class F> F fn(u64 bn) { return reinterpret_cast<F>(at(bn)); }
inline const u8* file_at(const char*,u64 bn) { return at(bn); }
inline void stub(u64 bn,const void* fn) {
    u8* p=at(bn);p[0]=0xff;p[1]=0x25;std::memset(p+2,0,4);std::memcpy(p+6,&fn,sizeof fn);
}
inline bool load() {
    const char* path=std::getenv("CACHYBORNE_TEST_ELF");
    if (!path) { std::puts("Set CACHYBORNE_TEST_ELF to the extracted 1.09 ELF"); return false; }
    std::ifstream f(path,std::ios::binary);
    std::vector<u8> bytes((std::istreambuf_iterator<char>(f)),{});
    assert(bytes.size()>sizeof(Elf64_Ehdr));
    Elf64_Ehdr eh;std::memcpy(&eh,bytes.data(),sizeof eh);
    assert(!std::memcmp(eh.e_ident,ELFMAG,SELFMAG) && eh.e_phentsize==sizeof(Elf64_Phdr));
    std::vector<Elf64_Phdr> ph(eh.e_phnum);
    assert(eh.e_phoff+ph.size()*sizeof(ph[0])<=bytes.size());
    std::memcpy(ph.data(),bytes.data()+eh.e_phoff,ph.size()*sizeof(ph[0]));
    std::size_t extent=0;
    for (auto p:ph) if (p.p_type==PT_LOAD || p.p_type==0x61000010)
        extent=std::max<std::size_t>(extent,p.p_vaddr+p.p_memsz);
    assert(extent>=0x53e3be8 && extent<(128u<<20));
    image=static_cast<u8*>(mmap(reinterpret_cast<void*>(0x400000),extent,
        PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0));
    assert(image!=MAP_FAILED);
    const u8* lib=nullptr;
    std::size_t libsize=0;
    std::map<u64,u64> tags;
    for (auto p:ph) {
        assert(p.p_offset+p.p_filesz<=bytes.size());
        if (p.p_type==PT_LOAD || p.p_type==0x61000010) std::memcpy(image+p.p_vaddr,bytes.data()+p.p_offset,p.p_filesz);
        if (p.p_type==0x61000000) { lib=bytes.data()+p.p_offset; libsize=p.p_filesz; }
        if (p.p_type==PT_DYNAMIC) for (u64 n=0;n<p.p_filesz;n+=16) {
            Elf64_Dyn d;std::memcpy(&d,bytes.data()+p.p_offset+n,sizeof d);
            if (!d.d_tag) break;
            tags[d.d_tag]=d.d_un.d_val;
        }
    }
    assert(lib);
    for (auto pair:{std::pair<u64,u64>{0x61000029,0x6100002d},{0x6100002f,0x61000031}}) {
        assert(tags[pair.first]+tags[pair.second]<=libsize);
        for (u64 n=0;n<tags[pair.second];n+=sizeof(Elf64_Rela)) {
            Elf64_Rela r;std::memcpy(&r,lib+tags[pair.first]+n,sizeof r);
            if (ELF64_R_TYPE(r.r_info)==R_X86_64_RELATIVE) {
                assert(r.r_offset+8<=extent);
                const u64 value=0x400000+r.r_addend;
                std::memcpy(image+r.r_offset,&value,8);
            }
        }
    }
    static u64 canary=0x123456789abcdef0;
    const auto guard=reinterpret_cast<u64>(&canary);
    std::memcpy(image+0x53e3be0,&guard,sizeof guard);
    return true;
}
// Seeded generator and special floats follow upstream's differential fixtures.
class Rng {
    std::mt19937_64 gen;
public:
    bool specials=false;
    explicit Rng(u64 seed):gen(seed) {}
    u64 next() { return gen(); }
    u32 u32() { return static_cast<::u32>(gen()); }
    int range(int lo,int hi) { return std::uniform_int_distribution<int>(lo,hi)(gen); }
    bool chance(double p) { return std::uniform_real_distribution<double>(0,1)(gen)<p; }
    float uniform(float lo,float hi) { return std::uniform_real_distribution<float>(lo,hi)(gen); }
    float value(float lo,float hi) {
        if (specials && chance(0.15)) {
            static const float values[]={0.0f,-0.0f,1.0f,-1.0f,0.5f,1e-40f,-1e-40f,FLT_MAX,-FLT_MAX,
                1e20f,-1e20f,127.0f,255.0f,2.0f,-2.0f,std::numeric_limits<float>::infinity(),
                -std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()};
            return values[range(0,int(std::size(values))-1)];
        }
        return uniform(lo,hi);
    }
};
}
