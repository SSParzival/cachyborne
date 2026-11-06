// SPDX-License-Identifier: GPL-3.0-or-later
// Compare the refund's unit price with the retail level-up menu at the exact
// affordability threshold. Adapted from BBHOST tests/decomp/player_data_test.
#include "retail_timing_kit.h"
#include "games/bloodborne/rebirth_plan.h"
#include "bbport_guest_call.h"
#include <array>

namespace {
alignas(16) std::array<u8,0x60> graph_row{};
struct ParamRef { s32 id;u32 pad;u64 row; };
void Param(ParamRef* out,s32 id) {
    assert(id==200);out->id=id;out->row=reinterpret_cast<u64>(graph_row.data());
}
}
int main() {
    if (!eboot_kit::load()) return 77;
    constexpr u8 entry[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec,0x18};
    assert(!std::memcmp(eboot_kit::at(0x1f2f5e0),entry,sizeof entry));
    eboot_kit::stub(0x2314b60,reinterpret_cast<void*>(&Param));
    alignas(16) std::array<u8,0x100> manager{},record{};
    alignas(16) std::array<u8,0x1000> screen{};
    std::array<u64,4> context{};
    s32 from=10,to=11,floor=10;
    context[1]=reinterpret_cast<u64>(screen.data());context[2]=reinterpret_cast<u64>(&floor);
    const auto man=reinterpret_cast<u64>(manager.data()),rec=reinterpret_cast<u64>(record.data());
    store<u64>(man+8,rec);store<u64>(0x593b130,man);
    const BbRebirth::CostGraph graph{.1f,1.f,.02f,92.f};
    std::memcpy(graph_row.data()+0x3c,graph.data(),sizeof(graph));
    const auto price=eboot_kit::fn<u64(*)(u64,const s32*,const s32*)>(0x1f2f5e0);
    unsigned differing=0;
    for (int level=1;level<=544;++level) {
        store<s32>(reinterpret_cast<u64>(screen.data())+0xe94,level);
        const auto cost=BbRebirth::LevelCost(level,graph);assert(cost);
        bool correct=true;
        for (s32 delta : {-1,0,1}) {
            store<s32>(rec+0x94,static_cast<s32>(*cost)+delta);
            const auto result=static_cast<s32>(price(reinterpret_cast<u64>(context.data()),&from,&to));
            BbGuestCall::Registers call{};
            call.fn=reinterpret_cast<void*>(price);
            call.ints[0]=reinterpret_cast<u64>(context.data());
            call.ints[1]=reinterpret_cast<u64>(&from);
            call.ints[2]=reinterpret_cast<u64>(&to);
            assert(BbGuestCall::Invoke(call));
            assert(static_cast<s32>(call.ret)==result);
            if (result!=(delta<0 ? from : to)) correct=false;
        }
        if (!correct) {
            if (differing<16) std::printf("DIFF: level %d planned unit cost %lld\n",level,static_cast<long long>(*cost));
            ++differing;
        }
    }
    std::printf("Retail price thresholds: %u differing levels of 544\n",differing);
    return differing ? 1 : 0;
}
