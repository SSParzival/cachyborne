// SPDX-License-Identifier: GPL-2.0-or-later
#include "games/bloodborne/bloodletter.h"
#include "param_repository_fixture.h"
#include <cassert>
#include <array>
#include <cstdio>

namespace {
using BbParams::Address;
struct Memory : ParamFixture::Memory {
    static constexpr Address File=Base+0x4000, Target=File+0x1000+328;
    unsigned writes=0;
    bool writable=true;
    void Attack() {
        Setup(5);
        const std::u16string name=u"AtkParam_Pc";
        Put<Address>(Base+0x2000+0x28,name.size());
        Bytes(Base+0x2100,name.c_str(),(name.size()+1)*2);
        Bytes(File+0xc,"ATK_PARAM_ST",13);
        for (unsigned i=0;i<3;++i) {
            Put<std::uint32_t>(File+0x40+i*0x18,2900929+i);
            Put<Address>(File+0x48+i*0x18,0x1000+i*164);
            Put<std::uint32_t>(File+0x1000+i*164,0x3ecccccd);
        }
    }
    static bool Write(void* context,Address at,const void* data,std::size_t size) {
        auto& self=*static_cast<Memory*>(static_cast<ParamFixture::Memory*>(context));
        ++self.writes;
        assert(at==Target && size==4);
        if (!self.writable) return false;
        self.Bytes(at,data,size);return true;
    }
};
}

int main() {
    using R=BbBloodletter::Result;
    Memory memory;memory.Attack();
    const auto original=memory.data;
    assert(BbBloodletter::Correct(memory.reader(),Memory::Write,Memory::Base)==R::Corrected);
    assert(memory.writes==1);
    for (unsigned i=0;i<memory.data.size();++i)
        if (i<Memory::Target-Memory::Base || i>=Memory::Target-Memory::Base+4)
            assert(memory.data[i]==original[i]);
    assert(BbBloodletter::Correct(memory.reader(),Memory::Write,Memory::Base)==R::AlreadyCorrected);
    assert(memory.writes==1);
    memory.Put<std::uint32_t>(Memory::Target,0x3f4ccccd); // A player's 0.8 m mod.
    assert(BbBloodletter::Correct(memory.reader(),Memory::Write,Memory::Base)==R::Custom);
    assert(memory.writes==1);
    memory.Put<std::uint32_t>(Memory::Target,0x3ecccccd);memory.writable=false;
    const auto before=memory.data;
    assert(BbBloodletter::Correct(memory.reader(),Memory::Write,Memory::Base)==R::WriteRefused);
    assert(memory.data==before);
    assert(BbBloodletter::Correct(memory.reader(),nullptr,Memory::Base)==R::WriteRefused);
    // Resolve the current file through the repository each time. The prior
    // corrected/custom row is never a borrowed address retained across loads.
    memory.Bytes(Memory::Base+0xa000,memory.data.data()+0x4000,0x2000);
    memory.Put<BbParams::Address>(Memory::Base+0x3000+0x70,Memory::Base+0xa000);
    memory.Put<std::uint32_t>(Memory::Base+0xb000+328,0x3f4ccccd);
    assert(BbBloodletter::Correct(memory.reader(),Memory::Write,Memory::Base)==R::Custom);
    assert(memory.writes==2);
    memory.Put<BbParams::Address>(Memory::Base+0x3000+0x70,Memory::File);
    memory.Bytes(Memory::File+0xc,"OTHER_PARAM",12);
    assert(BbBloodletter::Correct(memory.reader(),Memory::Write,Memory::Base)==R::Unavailable);
    assert(!BbBloodletter::Install(nullptr,0x6000000));
    std::puts("PASS: only original radius word, custom/idempotent/read-only/reload guards");
}
