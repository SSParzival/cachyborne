// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "games/bloodborne/params.h"
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace ParamFixture {
using BbParams::Address;
struct Memory {
    static constexpr Address Base=0x100000;
    std::vector<unsigned char> data=std::vector<unsigned char>(0x20000);
    unsigned reads=0;
    static bool Read(void* context,Address at,void* out,std::size_t size) {
        auto& self=*static_cast<Memory*>(context);++self.reads;
        if (at<Base || at-Base>self.data.size() || size>self.data.size()-(at-Base)) return false;
        std::memcpy(out,self.data.data()+at-Base,size);return true;
    }
    BbParams::Reader reader() { return {this,Read}; }
    template<class T> void Put(Address at,T value) { std::memcpy(data.data()+at-Base,&value,sizeof(value)); }
    void Bytes(Address at,const void* value,std::size_t size) { std::memcpy(data.data()+at-Base,value,size); }
    void Setup(unsigned format,bool short_name=false,bool wide_flags=true) {
        const Address repo=Base+0x100,cap=Base+0x2000,holder=Base+0x3000,file=Base+0x4000;
        Put(Base,repo);Put<std::int32_t>(repo+0x70+62*0x48,1);Put(repo+0x70+62*0x48+8,cap);
        const std::u16string name=short_name ? u"Short" : u"ReturnPointParam";
        Put<Address>(cap+0x28,name.size());Put<Address>(cap+0x30,short_name ? 7 : 31);
        if (short_name) Bytes(cap+0x18,name.c_str(),(name.size()+1)*2);
        else { Put(cap+0x18,Base+0x2100);Bytes(Base+0x2100,name.c_str(),(name.size()+1)*2); }
        Put(cap+0x70,holder);Put(holder+0x70,file);
        Put<std::uint16_t>(file+0xa,3);Bytes(file+0xc,"TEST_PARAM",11);
        Put<std::uint8_t>(file+0x2d,format);Put<std::uint8_t>(file+0x2e,wide_flags ? 2 : 0);
        const bool wide=format>3 && wide_flags;
        const Address start=format==2 ? 0x34 : 0x40;
        for (unsigned i=0;i<3;++i) {
            const Address record=file+start+i*(wide ? 0x18 : 0xc);
            Put<std::uint32_t>(record,i==2 ? 43 : 42); // retail tables can repeat IDs
            if (wide) Put<Address>(record+8,0x1000+i*16);
            else Put<std::uint32_t>(record+4,0x1000+i*16);
            Put<std::uint32_t>(file+0x1000+i*16,73+i);
        }
    }
};
}
