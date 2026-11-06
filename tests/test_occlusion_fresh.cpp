// SPDX-License-Identifier: GPL-2.0-or-later
// Execute the actual query writer: only accepted begins clear CPU results.
#include "retail_timing_kit.h"
#include "games/bloodborne/occlusion_fresh.h"
#include <array>

namespace {
bool accept=false;
unsigned refills=0;
alignas(16) std::array<u32,128> packets{};
alignas(16) std::array<u64,18> result{};
u8 Refill(u64 cb,u32 words,u64 argument) {
    // Unlike BBHOST's replacement, the retail call supplies three arguments;
    // RCX is not a fourth room argument at this site.
    assert(argument==0xcafe && (words==22 || words==8));
    ++refills;
    if (!accept) return 0;
    store<u64>(cb+8,reinterpret_cast<u64>(packets.data()+packets.size()));
    return 1;
}
}
int main(int argc,char** argv) {
    if (!eboot_kit::load()) return 77;
    if (argc==2) {
        const std::array<u64,4> sites{0x1077660,0x107769f,0x10776a5,0x10777d0};
        const int i=std::atoi(argv[1]);assert(i>=0 && i<4);
        auto* p=eboot_kit::image+sites[i];*p^=1;
        const std::vector<u8> before(eboot_kit::image+0x1077660,eboot_kit::image+0x10777e0);
        assert(!BbOcclusionFresh::Install(eboot_kit::image,0x6000000));
        assert(!std::memcmp(before.data(),eboot_kit::image+0x1077660,before.size()));
        std::puts("PASS: mismatching query writer refused without patching");return 0;
    }
    auto write=eboot_kit::fn<void(*)(u64,u32,u64)>(0x1477660);
    std::array<u64,6> cb{};
    const auto cbp=reinterpret_cast<u64>(cb.data());
    const auto block=reinterpret_cast<u64>(result.data()+1);
    struct Expected { std::array<u32,128> packets;u64 cursor;unsigned refills; };
    std::array<Expected,9> expected;
    const auto run=[&](int test,bool patched) {
        const unsigned kind=test/3,mode=test%3;
        packets.fill(0xdeadbeef);result.fill(0x8000000000000007ull);
        cb={1,reinterpret_cast<u64>(packets.data()+(mode ? 0 : packets.size())),
            reinterpret_cast<u64>(packets.data()),reinterpret_cast<u64>(&Refill),0xcafe,2};
        accept=mode==2;refills=0;
        write(cbp,kind,block);
        assert(result.front()==0x8000000000000007ull && result.back()==result.front());
        for (unsigned i=1;i<17;++i)
            assert(result[i]==(patched && kind==0 && mode!=1 ? 0 : 0x8000000000000007ull));
        if (!patched) expected[test]={packets,cb[2],refills};
        else {
            assert(packets==expected[test].packets && cb[2]==expected[test].cursor &&
                   refills==expected[test].refills);
        }
    };
    for (int i=0;i<9;++i) run(i,false);
    assert(!BbOcclusionFresh::Install(nullptr,0x6000000));
    assert(!BbOcclusionFresh::Install(eboot_kit::image,0x10777d4));
    assert(BbOcclusionFresh::Install(eboot_kit::image,0x6000000));
    for (int repeat=0;repeat<100;++repeat) for (int i=0;i<9;++i) run(i,true);
    std::puts("PASS: accepted begin clears stale results; end/other/refused retain them; retail packets and refill semantics unchanged");
}
