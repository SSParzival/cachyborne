// SPDX-License-Identifier: GPL-2.0-or-later
// Execute the complete retail constructor. Only its external memset import is
// bound to host memset, also observing the language count during construction.
#include "retail_timing_kit.h"
#include "games/bloodborne/fmod_project_init.h"

namespace {
u64 object=0;
u32 expected_count=0;
unsigned memset_calls=0;
void* __attribute__((sysv_abi)) Memset(void* dst,int value,size_t size) {
    assert(reinterpret_cast<u64>(dst)==object+0x178 && !value && size==0x110);
    assert(load<u32>(object+0x170)==expected_count);
    ++memset_calls;
    return std::memset(dst,value,size);
}
}
int main() {
    if (!eboot_kit::load()) return 77;
    auto* image=eboot_kit::image;
    using Constructor=void*(__attribute__((sysv_abi)) *)(void*);
    auto constructor=eboot_kit::fn<Constructor>(0x11f3ce0);
    eboot_kit::stub(0x2fbe688,reinterpret_cast<void*>(&Memset));
    alignas(16) std::array<u8,0x2a8> storage;
    object=reinterpret_cast<u64>(storage.data()+16);
    storage.fill(0xa5);
    for (const char* value : {"", "0", "true", "10"}) {
        setenv("BB_FMOD_PROJECT_INIT",value,1);
        assert(BbFmodProjectInit::Install(nullptr,0));
        assert(!BbFmodProjectInit::Read().enabled);
    }
    expected_count=0xa5a5a5a5;
    const auto before_return=constructor(reinterpret_cast<void*>(object));
    assert(load<u32>(object+0x170)==expected_count); // reproduced retail omission
    auto expected=storage;
    std::memset(expected.data()+16+0x170,0,4);

    setenv("BB_FMOD_PROJECT_INIT","1",1);
    assert(!BbFmodProjectInit::Install(nullptr,0));
    assert(!BbFmodProjectInit::Install(image,32));
    auto* entry=eboot_kit::at(0x11f3ce0);entry[0]^=1;
    assert(!BbFmodProjectInit::Install(image,0x6000000));
    entry[0]^=1;
    assert(BbFmodProjectInit::Install(image,0x6000000));
    assert(BbFmodProjectInit::Install(image,0x6000000));
    assert(!BbFmodProjectInit::Install(image+16,0x6000000));
    expected_count=0;
    for (unsigned repeat=0;repeat<3;++repeat) {
        storage.fill(0xa5);
        assert(constructor(reinterpret_cast<void*>(object))==before_return);
        assert(storage==expected); // precisely four additional bytes; guards intact
        store<u32>(object+0x170,2); // later successful LANG load can set the real count
        assert(load<u32>(object+0x170)==2);
    }
    const auto stats=BbFmodProjectInit::Read();
    assert(stats.enabled && stats.initialized==3 && memset_calls==4);
    std::puts("PASS: retail uninitialized count reproduced; exact four-byte initialization before original constructor, reuse, return forwarding and strict opt-in");
}
