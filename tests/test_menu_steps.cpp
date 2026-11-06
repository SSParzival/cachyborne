// SPDX-License-Identifier: GPL-2.0-or-later
#include "retail_timing_kit.h"
#include "games/bloodborne/menu_steps.h"
#include <array>
#include <atomic>

namespace {
unsigned destroyed=0;
void __attribute__((sysv_abi)) Destroy(std::uintptr_t) { ++destroyed; }
}
int main() {
    if (!eboot_kit::load()) return 77;
    auto* image=eboot_kit::image;
    constexpr auto size=0x6000000;
    assert(BbMenuSteps::Watch(image,32,7,u"ChrMakeCommandList")==-1);
    assert(BbMenuSteps::Watch(image,size,7,nullptr)==-1);
    assert(BbMenuSteps::Watch(image,size,7,u"")==-1);
    auto* entry=eboot_kit::at(0x201cce0);
    entry[0]^=1;
    assert(BbMenuSteps::Watch(image,size,7,u"ChrMakeCommandList")==-1);
    entry[0]^=1;
    const int appearance=BbMenuSteps::Watch(image,size,7,u"ChrMakeCommandList");
    const int level=BbMenuSteps::Watch(image,size,10,u"LevelUp");
    assert(appearance==0 && level==1);
    assert(BbMenuSteps::Watch(image,size,7,u"ChrMakeCommandList")==-1);
    assert(BbMenuSteps::Watch(image+16,size,8,u"Other")==-1);
    assert(!BbMenuSteps::Take(-1) && !BbMenuSteps::Take(8));
    alignas(16) std::array<u8,0x100> step{},other{};
    alignas(16) std::array<u8,0x30> null_factory{};
    using Constructor=void (*)(void*,u64,u64,const char16_t*,void*,void*,void*);
    const auto ctor=eboot_kit::fn<Constructor>(0x201cce0);
    const auto make=[&](auto& object,u64 type,const char16_t* name) {
        // The ENTIRE retail constructor runs, including all argument stores
        // and its zero-refcount initialization. Null factory functors use its
        // own empty branches; no continuation or guest call is stubbed.
        ctor(object.data(),123,type,name,null_factory.data(),null_factory.data(),null_factory.data());
        const auto address=reinterpret_cast<u64>(object.data());
        assert(load<u64>(address+0x20)==123 && load<u64>(address+0x28)==type);
        assert(load<u64>(address+0x30)==reinterpret_cast<u64>(name));
        assert(BbMenuSteps::Count(address)==0);
        return address;
    };
    make(step,6,u"ChrMakeCommandList");assert(!BbMenuSteps::Take(appearance));
    make(step,7,u"ChrMakeCommandListExtra");assert(!BbMenuSteps::Take(appearance));
    const auto address=make(step,7,u"ChrMakeCommandList");
    assert(BbMenuSteps::Take(appearance)==address && !BbMenuSteps::Take(appearance));
    assert(!BbMenuSteps::Hold(address)); // constructor result is not yet owned by a menu
    make(other,10,u"LevelUp");
    assert(BbMenuSteps::Take(level)==reinterpret_cast<u64>(other.data()));
    assert(!BbMenuSteps::Take(appearance));
    // Establish the game's menu-owned reference, then hold/drop the feature's.
    store<std::int32_t>(address+8,1);
    assert(BbMenuSteps::Hold(address) && BbMenuSteps::Count(address)==2);
    assert(BbMenuSteps::Release(address) && BbMenuSteps::Count(address)==1 && !destroyed);
    std::array<std::uintptr_t,1> vtable{reinterpret_cast<std::uintptr_t>(&Destroy)};
    store<std::uintptr_t>(address,reinterpret_cast<std::uintptr_t>(vtable.data()));
    assert(BbMenuSteps::Release(address) && BbMenuSteps::Count(address)==0 && destroyed==1);
    assert(!BbMenuSteps::Release(address) && !BbMenuSteps::Hold(address) && destroyed==1);
    store<std::int32_t>(address+8,INT32_MAX);assert(!BbMenuSteps::Hold(address));
    store<std::int32_t>(address+8,-1);assert(!BbMenuSteps::Hold(address) && !BbMenuSteps::Release(address));
    assert(!BbMenuSteps::Count(0) && !BbMenuSteps::Hold(0) && !BbMenuSteps::Release(0));
    for (u64 i=0;i<6;++i) assert(BbMenuSteps::Watch(image,size,100+i,u"Other")==int(i)+2);
    assert(BbMenuSteps::Watch(image,size,999,u"Full")==-1);
    std::puts("PASS: full retail menu constructor, typed/exact names, one-shot delivery and guest reference/destructor ownership");
}
