// SPDX-License-Identifier: GPL-2.0-or-later
// Actual retail pacer/menu observers with spies at event/stat guest entry points.
#include "retail_timing_kit.h"
#include "games/bloodborne/rebirth.h"
#include "games/bloodborne/rebirth_script.h"
#include "games/bloodborne/menu_steps.h"
#include "games/bloodborne/player_data.h"
#include "common/guest_memory.h"
#include <map>
#include <thread>
namespace {
std::map<u32,bool> flags;
unsigned gets=0,sets=0,applies=0,destroys=0;
u64 record=0,hp=0;
bool FlagGet(u64 unused,u32 id) { assert(!unused);++gets;return flags[id]; }
u64 FlagSet(u64 unused,u32 id,u8 value) { assert(!unused && value<=1);++sets;flags[id]=value;return 0; }
s64 Apply(void* captures) {
    ++applies;const auto at=reinterpret_cast<u64>(captures);
    store<s32>(record+0x90,load<s32>(at+8));
    const u64 offsets[]{0x40,0x48,0x58,0x60,0x68,0x70};
    for (unsigned i=0;i<6;++i) store<s32>(record+offsets[i],load<s32>(at+12+i*4));
    const s32 maximum=load<s32>(record+0x90)==10 ? 300 : 500;
    store<s32>(record+0x18,maximum);store<s32>(hp+0xfc,maximum);
    store<s32>(record+0x14,0);store<s32>(hp+0xf8,0); // prove heal happens after apply
    return 0;
}
void Destroy(u64) { ++destroys; }
struct Table {
    std::array<u8,0x90> cap{},holder{};
    std::array<char16_t,32> name{};
    std::vector<u8> file;
    void Setup(u64 repo,unsigned slot,std::u16string_view text,u32 id,unsigned row_size) {
        std::copy(text.begin(),text.end(),name.begin());
        const auto c=reinterpret_cast<u64>(cap.data()),h=reinterpret_cast<u64>(holder.data());
        store<s32>(repo+0x70+slot*0x48,1);store<u64>(repo+0x78+slot*0x48,c);
        store<u64>(c+0x18,reinterpret_cast<u64>(name.data()));store<u64>(c+0x28,text.size());store<u64>(c+0x30,text.size()+1);
        store<u64>(c+0x70,h);file.resize(0x70+2*row_size);store<u64>(h+0x70,reinterpret_cast<u64>(file.data()));
        const auto f=reinterpret_cast<u64>(file.data());
        store<std::uint16_t>(f+0xa,2);std::memcpy(file.data()+0xc,"TEST_PARAM",11);
        file[0x2d]=4;file[0x2e]=2;
        for (unsigned i=0;i<2;++i) { store<u32>(f+0x40+i*0x18,id+i);store<u64>(f+0x48+i*0x18,0x70+i*row_size); }
    }
};
}
int main() {
    if (!eboot_kit::load()) return 77;
    auto* image=eboot_kit::image;
    unsetenv("BB_REBIRTH");assert(!BbRebirth::Install(image,0x6000000));
    setenv("BB_REBIRTH","1",1);setenv("BB_ONLINE","1",1);
    assert(!BbRebirth::Install(image,0x6000000));unsetenv("BB_ONLINE");
    assert(!BbRebirth::Install(image,32));
    for (u64 at : {0x2013d00ull,0x1799bf0ull,0x172e5a0ull}) {
        eboot_kit::at(at)[0]^=1;assert(!BbRebirth::Install(image,0x6000000));eboot_kit::at(at)[0]^=1;
    }
    eboot_kit::at(0x2434770)[0]^=1;assert(!BbRebirth::Install(image,0x6000000));eboot_kit::at(0x2434770)[0]^=1;
    assert(BbRebirth::Install(image,0x6000000) && BbRebirth::Read().enabled);
    assert(BbRebirth::Install(image,0x6000000));assert(!BbRebirth::Install(image+16,0x6000000));
    eboot_kit::stub(0x2013d00,reinterpret_cast<void*>(&Apply));
    eboot_kit::stub(0x1799bf0,reinterpret_cast<void*>(&FlagGet));
    eboot_kit::stub(0x172e5a0,reinterpret_cast<void*>(&FlagSet));
    alignas(16) std::array<u8,0x70> world{};
    alignas(16) std::array<u8,0x400> chr{};
    alignas(16) std::array<u8,0x30> mods{},global{};
    alignas(16) std::array<u8,0x110> health{};
    alignas(16) std::array<u8,0x100> manager{};
    alignas(16) std::array<u8,0x1300> repository{};
    record=reinterpret_cast<u64>(mmap(nullptr,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
    assert(record!=u64(MAP_FAILED));hp=reinterpret_cast<u64>(health.data());
    const auto w=reinterpret_cast<u64>(world.data()),c=reinterpret_cast<u64>(chr.data());
    const auto m=reinterpret_cast<u64>(manager.data()),mod=reinterpret_cast<u64>(mods.data()),g=reinterpret_cast<u64>(global.data());
    store<u64>(0x593e878,w);store<u64>(w+0x60,c);store<u32>(c+0x3f8,0x18020000);
    store<u64>(c+0x3b0,mod);store<u64>(mod+0x20,hp);store<u64>(0x5956678,g);global[9]=123;
    store<u64>(0x593b130,m);store<u64>(m+8,record);store<u64>(0x593b100,m);
    store<u64>(0x5940340,reinterpret_cast<u64>(repository.data()));
    std::array<Table,2> tables;
    tables[0].Setup(reinterpret_cast<u64>(repository.data()),0,u"CharaInitParam",3000,0xd0);
    tables[1].Setup(reinterpret_cast<u64>(repository.data()),1,u"CalcCorrectGraph",200,0x50);
    auto* origin=tables[0].file.data()+0x70;
    const std::int16_t level=10;std::memcpy(origin+0xc0,&level,2);
    const unsigned offsets[]{0xc2,0xc3,0xc5,0xc6,0xc7,0xc8};const u8 stats[]{11,10,12,10,9,8};
    for (unsigned i=0;i<6;++i) origin[offsets[i]]=stats[i];
    const float graph[]{.1f,1.f,.02f,92.f};std::memcpy(tables[1].file.data()+0x70+0x3c,graph,sizeof graph);
    store<s32>(record+0x90,11);store<s32>(record+0x94,1000);store<s32>(record+0x18,500);
    store<s32>(hp+0xfc,500);const u64 stat_offsets[]{0x40,0x48,0x58,0x60,0x68,0x70};
    for (unsigned i=0;i<6;++i) store<s32>(record+stat_offsets[i],stats[i]+(i==0));
    const auto initial=BbPlayerData::Read({nullptr,BbGuestMemory::Read},0x593b130);assert(initial);
    store<u32>(m+0x94,0xffffffff);
    assert(BbPlayerData::Stat({nullptr,BbGuestMemory::Read},*initial,"play_time_ms")==0xffffffffll);
    assert(!BbPlayerData::Stat({nullptr,BbGuestMemory::Read},*initial,"bogus"));
    assert(!BbPlayerData::Read({nullptr,BbGuestMemory::Read},0));
    assert(mprotect(reinterpret_cast<void*>(0x2434000),4096,PROT_READ|PROT_WRITE|PROT_EXEC)==0);
    constexpr u8 unwind[]{0x48,0x83,0xc4,0x38,0x5b,0x41,0x5c,0x41,0x5d,0x41,0x5e,0x41,0x5f,0x5d,0xc3};
    std::memcpy(eboot_kit::at(0x2434781),unwind,sizeof unwind);
    const auto frame=eboot_kit::fn<void(*)()>(0x2434770);
    frame();assert(sets==4);
    flags[rebirth::kReqReset]=true;
    std::thread service([&]{frame();});service.join();assert(applies==0);
    frame();assert(applies==1 && load<s32>(record+0x90)==10 && load<s32>(record+0x94)==1829);
    assert(load<s32>(record+0x14)==300 && load<s32>(hp+0xf8)==300 && !flags[rebirth::kReqReset]);
    flags[rebirth::kReqUndo]=true;frame();assert(applies==2 && load<s32>(record+0x90)==11 && load<s32>(record+0x94)==1000);
    assert(load<s32>(record+0x14)==500 && load<s32>(hp+0xf8)==500);
    store<s32>(record+0x94,999999999);flags[rebirth::kReqReset]=true;frame();assert(applies==2 && flags[rebirth::kFail]);
    store<s32>(record+0x94,1000);
    assert(mprotect(reinterpret_cast<void*>(record),4096,PROT_READ)==0);
    flags[rebirth::kReqReset]=true;frame();assert(applies==2 && flags[rebirth::kFail]);
    assert(mprotect(reinterpret_cast<void*>(record),4096,PROT_READ|PROT_WRITE)==0);
    store<u64>(mod+0x20,0);flags[rebirth::kReqReset]=true;frame();assert(applies==2);store<u64>(mod+0x20,hp);
    flags[rebirth::kReqReset]=true;frame();assert(applies==3);
    using Ctor=void(*)(void*,u64,u64,const char16_t*,void*,void*,void*);
    alignas(16) std::array<u8,0x100> menu{};alignas(16) std::array<u8,0x30> factory{};
    eboot_kit::fn<Ctor>(0x201cce0)(menu.data(),0,7,u"LevelUp",factory.data(),factory.data(),factory.data());
    const auto step=reinterpret_cast<u64>(menu.data());std::array<u64,1> vtable{reinterpret_cast<u64>(&Destroy)};
    store<u64>(step,reinterpret_cast<u64>(vtable.data()));store<s32>(step+8,1);
    flags[rebirth::kMenu]=true;frame();assert(BbMenuSteps::Count(step)==2);
    assert(BbMenuSteps::Release(step));frame();assert(destroys==1 && !flags[rebirth::kMenu]);
    global[8]=1;frame();global[8]=0;frame();
    flags[rebirth::kReqUndo]=true;frame();assert(applies==3 && flags[rebirth::kFail]);
    store<u32>(c+0x3f8,0x1a000000);frame();const auto before_gets=gets;
    for (unsigned i=0;i<10000;++i) frame();assert(gets==before_gets);
    const auto report=BbRebirth::Read();assert(report.resets==2 && report.undos==1 && report.failures==3);
    assert(munmap(reinterpret_cast<void*>(record),4096)==0);
    std::puts("PASS: retail pacer/menu observers, shared flags/player reads, reset/refund/heal/undo, cap/readonly guards, world cleanup and thread ownership");
}
