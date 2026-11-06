// SPDX-License-Identifier: GPL-2.0-or-later
#include "retail_timing_kit.h"
#include "bbport_limiter.h"
#include "bbport_frame_rate.h"
#include "bbport_settings.h"
#include <array>
#include <vector>

namespace {
std::uint64_t virtual_us;
unsigned foreground;
std::vector<std::uint32_t> sleeps;
std::uint64_t Foreground(void*) { return foreground; }
int Time(void* result,void*) {
    const std::array<std::int64_t,2> now{std::int64_t(virtual_us/1000000),std::int64_t(virtual_us%1000000)};
    std::memcpy(result,now.data(),sizeof(now));++virtual_us;return 0;
}
int Sleep(std::uint32_t us) { sleeps.push_back(us);virtual_us+=us;return 0; }
using Pacer=std::array<u8,0x2c8>;
Pacer State(unsigned mode,unsigned seed) {
    Pacer result{};const auto at=reinterpret_cast<u64>(result.data());
    store<std::uint32_t>(at+8,mode);store<std::uint32_t>(at+0xc,(mode+1)%5);
    store<std::uint64_t>(at+0x28,1000000);
    store<std::int32_t>(at+0x2bc,-1);store<std::int32_t>(at+0x2c0,-1);
    store<std::uint8_t>(at+0x271,seed%3==0);store<std::uint8_t>(at+0x272,seed%7==0);
    store<std::uint8_t>(at+0x273,seed%11==0);store<std::uint8_t>(at+0x276,seed%13==0);
    store<std::uint8_t>(at+0x2c4,seed%17==0);
    store<std::uint32_t>(at+0x260,seed%32);
    for (unsigned i=0;i<32;++i) store<std::uint64_t>(at+0x60+i*16,16000+i*13);
    for (unsigned i=0;i<16;++i) store<float>(at+0x278+i*4,1.0f/60);
    return result;
}
}

int main(int argc,char** argv) {
    if (!eboot_kit::load()) return 77;
    const bool mismatch=argc>1 && std::strcmp(argv[1],"mismatch")==0;
    const int mode=argc>1 && !mismatch ? std::atoi(argv[1]) : -1;
    if (mismatch) {
        const std::array<unsigned,8> sites{0x2034ada,0x2034b9e,0x2034ba8,0x2034bb2,0x2034bd2,
            0x4929390,0x4929394,0x4929398};
        const unsigned index=argc>2 ? unsigned(std::atoi(argv[2])) : 0;
        const unsigned offset=sites[index%sites.size()];
        eboot_kit::image[offset]^=1;
        std::array<u8,0x700> before{};
        std::memcpy(before.data(),eboot_kit::image+0x2034770,before.size());
        assert(!BbLimiter::Install(eboot_kit::image,0x6000000,-1));
        assert(std::memcmp(before.data(),eboot_kit::image+0x2034770,before.size())==0);
        std::puts("Limiter mismatch refused without changing the pacer");return 0;
    }
    eboot_kit::stub(0x13e3980,reinterpret_cast<const void*>(Foreground));
    eboot_kit::stub(0x2fbe728,reinterpret_cast<const void*>(Time));
    eboot_kit::stub(0x2fbfe68,reinterpret_cast<const void*>(Sleep));
    store<u64>(0x5940500,0x100000);
    if (argc>2) {
        setenv("BB_FPS","60",1);setenv("BB_FPS_LIMIT","60",1);
        unsetenv("BB_FRAME_HOOKS");
        unsetenv("BB_LIMITER_MARGIN_US");
        BbSettings::Get().frame_clock=false;
        BbFrameRate::PatchImage(eboot_kit::image,0x6000000);
        assert(BbFrameRate::Unlocked());
    }
    const auto update=eboot_kit::fn<void (*)(void*)>(0x2434770);
    struct Sample { Pacer initial,expected,late_expected;std::uint64_t start;unsigned foreground; };
    std::vector<Sample> samples;
    for (unsigned fg=0;fg<2;++fg) for (unsigned m=0;m<5;++m) for (unsigned seed=0;seed<40;++seed) {
        Sample sample{State(m,seed),{},{},1000000+(seed*937)%40000,fg};
        sample.expected=sample.initial;foreground=fg;virtual_us=sample.start;sleeps.clear();
        update(sample.expected.data());
        // A zero spin margin sleeps to the deadline and pays the next clock
        // read's one virtual microsecond. Replay that release through the
        // original code too; all fields must still agree at the same time.
        sample.late_expected=sample.initial;
        virtual_us=Load<std::uint64_t>(reinterpret_cast<u64>(sample.expected.data())+0x28)+1;
        update(sample.late_expected.data());samples.push_back(sample);
    }
    assert(BbLimiter::Install(eboot_kit::image,0x6000000,mode));
    unsigned sleeping_foreground=0,zero_margin_late=0;
    for (const auto& sample:samples) {
        auto actual=sample.initial;foreground=sample.foreground;virtual_us=sample.start;sleeps.clear();
        update(actual.data());
        if (actual!=sample.expected) {
            if (mode==0 && actual==sample.late_expected) ++zero_margin_late;
            else {
                for (unsigned i=0;i<actual.size();++i) if (actual[i]!=sample.expected[i])
                    std::fprintf(stderr,"Limiter mismatch byte %#x: %u != %u\n",i,actual[i],sample.expected[i]);
                assert(false);
            }
        }
        if (foreground && !sleeps.empty()) ++sleeping_foreground;
    }
    assert(sleeping_foreground>0);
    std::printf("Limiter retail: %zu original-equivalent pacer states, foreground sleeping in %u cases; zero-margin 1-us releases %u\n",samples.size(),sleeping_foreground,zero_margin_late);
}
