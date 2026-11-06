// SPDX-License-Identifier: GPL-2.0-or-later
#include "games/bloodborne/params.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <limits>
#include <vector>

#include "param_repository_fixture.h"
using ParamFixture::Memory;
using BbParams::Address;

int main() {
    for (unsigned format=0;format<=5;++format) for (bool short_name : {false,true}) for (bool wide : {false,true}) {
        Memory memory;memory.Setup(format,short_name,wide);
        const auto table=BbParams::FindTable(memory.reader(),Memory::Base,short_name ? "Short" : "ReturnPointParam");
        assert(table && table->type=="TEST_PARAM" && table->rows==3);
        std::size_t count=0;
        auto reads=memory.reads;
        assert(BbParams::ListIds(memory.reader(),*table,nullptr,SIZE_MAX,&count));
        assert(count==3 && memory.reads==reads);
        std::uint32_t ids[4]={99,99,99,99};
        assert(BbParams::ListIds(memory.reader(),*table,ids,0,&count));
        assert(ids[0]==99 && memory.reads==reads);
        assert(BbParams::ListIds(memory.reader(),*table,ids,2,&count));
        assert(count==3 && ids[0]==42 && ids[1]==42 && ids[2]==99);
        assert(BbParams::ListIds(memory.reader(),*table,ids,SIZE_MAX));
        assert(ids[2]==43 && ids[3]==99);
        const auto row=BbParams::FindRow(memory.reader(),*table,42);
        assert(row && row->bytes==16 && BbParams::ReadWord(memory.reader(),*row,0)==73);
        assert(!BbParams::ReadWord(memory.reader(),*row,13));
        assert(!BbParams::ReadWord(memory.reader(),*row,std::numeric_limits<std::size_t>::max()));
        assert(!BbParams::FindRow(memory.reader(),*table,99));
        assert(!BbParams::FindTable(memory.reader(),Memory::Base,"Missing"));
        // A row pointing into the header is refused before dereferencing it.
        if (format>3 && wide) memory.Put<Address>(table->file+0x48,1);
        else memory.Put<std::uint32_t>(table->file+(format==2 ? 0x38 : 0x44),1);
        assert(!BbParams::FindRow(memory.reader(),*table,42));
        assert(!BbParams::ListIds(memory.reader(),*table,ids,3));
    }
    Memory memory;memory.Setup(5);
    // Resolve the current holder on every query rather than retaining a file
    // pointer from before an asset reload.
    memory.Bytes(Memory::Base+0xa000,memory.data.data()+0x4000,0x2000);
    memory.Put<Address>(Memory::Base+0x3000+0x70,Memory::Base+0xa000);
    memory.Put<std::uint32_t>(Memory::Base+0xb000,91);
    const auto reloaded=BbParams::FindTable(memory.reader(),Memory::Base,"ReturnPointParam");
    assert(reloaded && reloaded->file==Memory::Base+0xa000);
    const auto row=BbParams::FindRow(memory.reader(),*reloaded,42);
    assert(row && BbParams::ReadWord(memory.reader(),*row,0)==91);
    auto before=memory.reads;
    assert(!BbParams::FindTable(memory.reader(),std::numeric_limits<Address>::max()-3,"ReturnPointParam"));
    assert(memory.reads==before); // no wrapped callback address
    memory.Put<Address>(Memory::Base+0x2000+0x18,0);
    assert(!BbParams::FindTable(memory.reader(),Memory::Base,"ReturnPointParam"));
    // Thousands of records, deliberately descending: preserve lookup semantics
    // without one safe-read syscall per integer/pointer. Batch size bounds both
    // stack use and read size even on large or malformed tables.
    Memory large;large.data.resize(0x40000);large.Setup(5);
    const auto table=BbParams::FindTable(large.reader(),Memory::Base,"ReturnPointParam");
    assert(table);
    constexpr unsigned Count=2049;
    large.Put<std::uint16_t>(table->file+0xa,Count);
    auto big=*table;big.rows=Count;
    for (unsigned i=0;i<Count;++i) {
        large.Put<std::uint32_t>(big.file+0x40+i*0x18,Count-i);
        large.Put<Address>(big.file+0x48+i*0x18,0x10000+i*16);
        large.Put<std::uint32_t>(big.file+0x10000+i*16,i);
    }
    before=large.reads;
    const auto last=BbParams::FindRow(large.reader(),big,17);
    assert(last && last->address==big.file+0x10000+(Count-17)*16 && last->bytes==16);
    assert(large.reads-before<=20); // 2 stride reads + 16 bounded batches.
    std::vector<std::uint32_t> ids(Count+1,0xdeadbeef);
    std::size_t count=0;
    before=large.reads;
    assert(BbParams::ListIds(large.reader(),big,ids.data(),SIZE_MAX,&count));
    assert(count==Count && large.reads-before==17);
    for (unsigned i=0;i<Count;++i) assert(ids[i]==Count-i);
    assert(ids.back()==0xdeadbeef);
    large.Put<std::uint32_t>(big.file+0x40+511*0x18,17);
    const auto first_duplicate=BbParams::FindRow(large.reader(),big,17);
    assert(first_duplicate && first_duplicate->address==big.file+0x10000+511*16);
    assert(BbParams::ListIds(large.reader(),big,ids.data(),Count));
    assert(ids[511]==17 && ids[Count-17]==17);
    large.Put<Address>(big.file+0x48+510*0x18,1);
    assert(!BbParams::FindRow(large.reader(),big,17));
    assert(!BbParams::ListIds(large.reader(),big,ids.data(),Count));
    // A truncated metadata array must report failure, including across batches.
    large.data.resize(0x4000+0x40+128*0x18);
    assert(!BbParams::ListIds(large.reader(),big,ids.data(),129));
    auto invalid=big;invalid.format=6;
    assert(!BbParams::ListIds(large.reader(),invalid,nullptr,0));
    invalid=big;invalid.file=std::numeric_limits<Address>::max()-3;
    before=large.reads;
    assert(!BbParams::ListIds(large.reader(),invalid,ids.data(),1));
    assert(large.reads==before);
    std::puts("Read-only param repository: row lookup/enumeration, bounded batches, formats, reloads, duplicate IDs and invalid ranges passed");
}
