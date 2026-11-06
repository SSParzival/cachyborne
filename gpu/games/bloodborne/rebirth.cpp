// SPDX-License-Identifier: GPL-3.0-or-later
// Runtime altar service adapted from BBHOST engine/rebirth.cpp at
// bdc5cdf8be5f37188da9c2368e92be444fdc04cb. Offline assets are prepared separately.
#include "rebirth.h"
#include "rebirth_session.h"
#include "player_data.h"
#include "menu_steps.h"
#include "event_flags.h"
#include "common/guest_memory.h"
#include "shim/bbport_frame_tasks.h"
#include "shim/bb_env.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>

namespace BbRebirth {
namespace {
using Address=std::uintptr_t;
constexpr Address ApplyStats=0x1c13d00,World=0x553e878,GameData=0x553b130,Repository=0x5540340,Global=0x5556678;
using BbGuestMemory::Read;
const BbParams::Reader Reader{nullptr,BbGuestMemory::Read};
Address Pointer(Address at) { Address p=0;Read(at,p);return p; }
bool Writable(Address at,std::size_t size) {
    if (!BbGuestMemory::Range(at,size)) return false;
    std::ifstream maps("/proc/self/maps");std::string line;
    while (std::getline(maps,line)) {
        unsigned long lo=0,hi=0;char permissions[5]{};
        if (std::sscanf(line.c_str(),"%lx-%lx %4s",&lo,&hi,permissions)==3 && at>=lo && at<hi)
            return permissions[1]=='w' && size<=hi-at;
    }
    return false;
}
struct Runtime final : Backend {
    Address base=0;int watch=-1;
    Session session;
    std::atomic<pid_t> owner{0};
    std::atomic<bool> enabled{false};
    std::atomic<std::uint64_t> resets{0},undos{0},failures{0};
    bool World(std::uint32_t& block) override {
        const auto state=Pointer(base+Global);
        std::uint8_t loading=1;
        if (!state || !Read(state+8,loading) || loading) return false;
        const auto manager=Pointer(base+::BbRebirth::World);
        const auto player=manager ? Pointer(manager+0x60) : 0;
        return player && Read(player+0x3f8,block) && block && block!=0xffffffff;
    }
    Address TakeMenu() override { return BbMenuSteps::Take(watch); }
    bool ReadFlags(std::array<bool,3>& flags) override {
        constexpr std::array<std::uint32_t,3> ids{rebirth::kReqReset,rebirth::kReqUndo,rebirth::kMenu};
        return BbEventFlags::Read(base,ids,flags);
    }
    bool Flag(std::uint32_t id,bool value) override { return BbEventFlags::Set(base,id,value); }
    bool Hold(Address menu) override {
        std::uint64_t type=0;std::int32_t refs=0;
        if (!Read(menu+8,refs) || refs<1 || !Read(menu+0x28,type) || type!=7) return false;
        const auto name=Pointer(menu+0x30);
        char16_t text[8]{};
        return name && BbGuestMemory::Read(nullptr,name,text,sizeof text) &&
            !std::memcmp(text,u"LevelUp",sizeof text) && BbMenuSteps::Hold(menu);
    }
    std::int32_t Count(Address menu) override { return BbMenuSteps::Count(menu); }
    void Release(Address menu) override { BbMenuSteps::Release(menu); }
    struct Health { Address module;std::int32_t record_max; };
    std::optional<Health> Preflight(const BbPlayerData::Player& p) {
        const auto manager=Pointer(base+::BbRebirth::World);
        const auto chr=manager ? Pointer(manager+0x60) : 0;
        const auto mods=chr ? Pointer(chr+0x3b0) : 0;
        const auto hp=mods ? Pointer(mods+0x20) : 0;
        std::int32_t maximum=0,record_max=0;
        if (!hp || !Read(hp+0xfc,maximum) || maximum<=0 ||
            !Read(p.record+0x18,record_max) || record_max<=0 ||
            !Writable(hp+0xf8,4) || !Writable(p.record+0x14,4) || !Writable(p.record+0x94,4)) return {};
        return Health{hp,record_max};
    }
    void Apply(const Build& build) {
        alignas(16) std::array<std::uint32_t,10> captures{};
        captures[2]=build.level;
        for (unsigned i=0;i<6;++i) captures[3+i]=build.stats[i];
        reinterpret_cast<std::int64_t(__attribute__((sysv_abi)) *)(void*)>(base+ApplyStats)(captures.data());
    }
    bool Finish(const BbPlayerData::Player& player,const Health& health,const Build& build) {
        std::int32_t maximum=0,record_max=0;
        if (!Read(health.module+0xfc,maximum) || maximum<=0 ||
            !Read(player.record+0x18,record_max) || record_max<=0) return false;
        const auto echoes=static_cast<std::int32_t>(build.echoes);
        return BbGuestMemory::Write(health.module+0xf8,&maximum,4) &&
            BbGuestMemory::Write(player.record+0x14,&record_max,4) &&
            BbGuestMemory::Write(player.record+0x94,&echoes,4);
    }
    bool Change(const BbPlayerData::Player& p,const Build& target) {
        const auto valid=[](const Build& b) {
            return b.level>=1 && b.level<=MaxLevel && ValidStats(b.stats) && b.echoes>=0 && b.echoes<=EchoCap;
        };
        if (!valid(p.build) || !valid(target)) return false;
        const auto health=Preflight(p);if (!health) return false;
        Apply(target);
        if (Finish(p,*health,target)) return true;
        Apply(p.build);
        if (!Finish(p,*health,p.build)) {
            enabled.store(false,std::memory_order_release);
            std::fputs("Rebirth: rollback could not complete; altar service disabled\n",stderr);
        }
        return false;
    }
    bool Reset(Snapshot& before) override {
        const auto player=BbPlayerData::Read(Reader,base+GameData);
        if (!player) { ++failures;return false; }
        auto row=[&](const char* table,std::uint32_t id,void* destination,std::size_t size) {
            const auto t=BbParams::FindTable(Reader,base+Repository,table);
            const auto r=t ? BbParams::FindRow(Reader,*t,id) : std::nullopt;
            return r && r->bytes>=size && BbGuestMemory::Read(nullptr,r->address,destination,size);
        };
        std::array<std::uint8_t,0xc9> origin_row{};
        std::array<std::uint8_t,0x4c> graph_row{};
        if (!row("CharaInitParam",3000u+player->origin,origin_row.data(),origin_row.size()) ||
            !row("CalcCorrectGraph",200,graph_row.data(),graph_row.size())) { ++failures;return false; }
        const auto origin=DecodeOrigin(origin_row);const auto graph=DecodeGraph(graph_row);
        const auto plan=origin && graph ? PlanReset(player->build,*origin,*graph) : std::nullopt;
        if (!plan || !Change(*player,plan->after)) { ++failures;return false; }
        before={plan->before,player->record};++resets;
        std::printf("Rebirth: reset level %d to %d; refunded %lld echoes\n",plan->before.level,plan->after.level,static_cast<long long>(plan->refund));
        return true;
    }
    bool Restore(const Snapshot& before) override {
        const auto player=BbPlayerData::Read(Reader,base+GameData);
        if (!player || player->record!=before.record || !Change(*player,before.build)) { ++failures;return false; }
        ++undos;std::puts("Rebirth: original attributes and echoes restored");return true;
    }
};
Runtime& Get() { static auto* const state=new Runtime;return *state; }
void Tick() {
    auto& state=Get();if (!state.enabled.load(std::memory_order_acquire)) return;
    const pid_t tid=gettid();pid_t zero=0;state.owner.compare_exchange_strong(zero,tid);
    if (state.owner.load()!=tid) return;
    const auto now=std::chrono::steady_clock::now().time_since_epoch();
    state.session.Tick(state,std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}
}
bool Install(unsigned char* image,std::uint64_t size) {
    if (!bb_env_on("BB_REBIRTH") || bb_env_on("BB_ONLINE") || bb_env_on("BB_BBSC")) return false;
    auto& state=Get();
    if (state.enabled.load(std::memory_order_acquire)) return state.base==reinterpret_cast<Address>(image);
    constexpr unsigned char apply[]{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec,0x28};
    if (!image || size<Global+8 || std::memcmp(image+ApplyStats,apply,sizeof apply) || !BbEventFlags::Validate(image,size)) return false;
    if (!BbFrameTasks::Subscribe(image,size,Tick)) return false;
    const auto watch=BbMenuSteps::Watch(image,size,7,u"LevelUp");if (watch<0) return false;
    state.base=reinterpret_cast<Address>(image);state.watch=watch;
    state.enabled.store(true,std::memory_order_release);
    std::puts("Rebirth: offline altar service enabled (prepared script/message layer required)");return true;
}
Stats Read() { auto& s=Get();return {s.resets.load(),s.undos.load(),s.failures.load(),s.enabled.load()}; }
}
