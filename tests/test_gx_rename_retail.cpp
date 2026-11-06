// SPDX-License-Identifier: GPL-2.0-or-later
// Execute the retail rename body with controlled allocator and reclaim calls.
// A reclaim visit does NOT prove that a later return 9 is safe to retry:
// reclaim can recover the copy, followed by a table allocation failure.
#include "retail_timing_kit.h"
#include <array>

namespace {
unsigned attempts=0, copies=0, walks=0;
bool recover=false, table_available=false;
alignas(16) std::array<u64,2> copy_block{0,0x12345678};
alignas(16) std::array<u64,8> table_block{};
u64 Copy(u64, u32 size, u32 alignment) {
    assert(size==64 && alignment==16);
    ++attempts;
    if (!recover || !walks) return 0;
    ++copies;
    return reinterpret_cast<u64>(copy_block.data());
}
u64 Table(u64, u64 size, u32 alignment) {
    assert(size==56 && alignment==8);
    return table_available ? reinterpret_cast<u64>(table_block.data()) : 0;
}
void Reclaim(u64, u64) { ++walks; }
u64 ResourceId(u64) { return 0xabcdef; }
}

int main() {
    if (!eboot_kit::load()) return 77;
    eboot_kit::stub(0x2aaa860,reinterpret_cast<void*>(&Reclaim));
    eboot_kit::stub(0x256e1b0,reinterpret_cast<void*>(&ResourceId));
    auto rename=eboot_kit::fn<u32(*)(u64,u64,u64,u64)>(0x2ad0d30);
    std::array<u64,12> vtable{};
    vtable[3]=reinterpret_cast<u64>(&Copy);
    vtable[11]=reinterpret_cast<u64>(&Table);
    std::array<u64,2> allocator{reinterpret_cast<u64>(vtable.data()),0};
    std::array<u64,2> indirect{0,reinterpret_cast<u64>(allocator.data())};
    std::vector<u8> context(0xb4a0),resource(0x30),table(0x30);
    const u64 ctx=reinterpret_cast<u64>(context.data());
    const u64 res=reinterpret_cast<u64>(resource.data());
    const u64 tab=reinterpret_cast<u64>(table.data());
    store<u64>(ctx+0xb490,reinterpret_cast<u64>(indirect.data()));
    store<u32>(res+0x14,64);
    const auto reset=[&] {
        attempts=copies=walks=0;
        std::fill(table.begin(),table.end(),0);
        store<u64>(tab,reinterpret_cast<u64>(allocator.data()));
        store<u32>(tab+0x18,2);
    };
    u64 out=0xdeadbeef;
    reset();
    assert(rename(tab,ctx,reinterpret_cast<u64>(&out),res)==9);
    assert(attempts==2 && walks==1 && copies==0 && out==0xdeadbeef);
    recover=true;
    reset();
    assert(rename(tab,ctx,reinterpret_cast<u64>(&out),res)==9);
    assert(attempts==2 && walks==1 && copies==1 && out==0xdeadbeef);
    // Repeating this failure consumes a second copy. Counting reclaim visits
    // alone would incorrectly classify the previous result as copy exhaustion.
    assert(rename(tab,ctx,reinterpret_cast<u64>(&out),res)==9);
    assert(copies==2 && out==0xdeadbeef);
    table_available=true;
    reset();
    assert(rename(tab,ctx,reinterpret_cast<u64>(&out),res)==0);
    assert(walks==1 && copies==1 && out==copy_block[1]);
    assert(load<u64>(reinterpret_cast<u64>(table_block.data())+16)==reinterpret_cast<u64>(copy_block.data()));
    std::puts("PASS: retail copy exhaustion, reclaim recovery followed by table failure, unsafe retry consumption and successful rename");
}
