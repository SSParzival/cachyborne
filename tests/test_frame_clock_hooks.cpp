// SPDX-License-Identifier: GPL-2.0-or-later
#include "retail_timing_kit.h"
#include "bbport_frame_rate.h"
#include "bbport_settings.h"
#include "bbport_guest_code.h"
#include "bbport_sound_wake.h"
#include <array>
#include <chrono>

namespace {
float driven;
u64 KeyFrame(u64, u64 bodies, u64, u64) {
    driven = load<float>(bodies + 0x10);
    return 42;
}
void Writable(u64 at) {
    assert(mprotect(reinterpret_cast<void*>(at & ~u64(4095)),4096,
                    PROT_READ|PROT_WRITE|PROT_EXEC)==0);
}
}

int main(int argc, char**) {
    if (!eboot_kit::load()) return 77;
    const bool native = argc == 1;
    const bool wind = argc < 3;
    const bool wind_mismatch = argc > 3;
    setenv("BB_FPS","uncap",1);
    setenv("BB_FPS_LIMIT","480",1);
    unsetenv("BB_FRAME_HOOKS");
    unsetenv("BB_FRAME_DT_SMOOTH");
    unsetenv("BB_EFFECT_TIME");
    if (wind || wind_mismatch) unsetenv("BB_WIND_PACE"); else setenv("BB_WIND_PACE","0",1);
    std::array<u8,8> original_force;
    std::memcpy(original_force.data(),eboot_kit::at(0x27149ea),original_force.size());
    if (wind_mismatch) {
        original_force.back()^=1;
        std::memcpy(eboot_kit::at(0x27149ea),original_force.data(),original_force.size());
    }
    BbSettings::Get().frame_clock = native;
    BbFrameRate::PatchImage(eboot_kit::image,0x6000000);
    assert(BbFrameRate::Unlocked());
    assert(BbSoundWake::Enabled() == native);

    // Enter the actual installed main-loop step hook, with its real RBX/RBP
    // contract. Only the continuation is a return, so the test does not run a
    // whole game loop or need Vulkan. The pacer dt passes through FrameStep.
    Writable(0x2418e3d);
    *eboot_kit::at(0x2418e3d) = 0xc3;
    BbGuestCode::Emitter invoke;
    invoke.Bytes({0x55,0x48,0x89,0xe5,0x53,0x48,0x83,0xec,0x48,0x48,0x89,0xfb});
    invoke.CallAbs(eboot_kit::at(0x2418e36));
    invoke.Bytes({0x48,0x83,0xc4,0x48,0x5b,0x5d,0xc3});
    auto* thunk = static_cast<u8*>(mmap(nullptr,4096,PROT_READ|PROT_WRITE,
                                       MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
    assert(thunk != MAP_FAILED && !invoke.overflow);
    std::memcpy(thunk,invoke.code.data(),invoke.size);
    assert(mprotect(thunk,4096,PROT_READ|PROT_EXEC)==0);
    const auto step = reinterpret_cast<void (*)(void*)>(thunk);
    alignas(16) std::array<u8,0x300> pacer{};
    alignas(16) std::array<u8,0x800> effect{};
    store<u64>(0x5865ed0,reinterpret_cast<u64>(effect.data()));
    const auto yebis = eboot_kit::fn<u64 (*)(u64,float)>(0xfb2800);
    alignas(16) std::array<u8,0x100> controller{};
    alignas(16) std::array<u8,0x40> ragdoll{};
    const auto self = reinterpret_cast<u64>(controller.data());
    store<float>(self+0x18,1);
    store<u64>(self+0x50,reinterpret_cast<u64>(ragdoll.data()));
    store<u64>(reinterpret_cast<u64>(ragdoll.data())+0x18,1);
    eboot_kit::stub(0xae9b90,reinterpret_cast<const void*>(KeyFrame));
    const auto drive = eboot_kit::fn<void (*)(u64)>(0xae4600);
    // Execute the actual wind loads at their retail addresses, followed only
    // by a test return. No other part of the swaying-model update is mocked.
    Writable(0x2713d95);
    constexpr u8 wind_return[]{0xc5,0xf8,0x28,0xc4,0xc3}; // vmovaps xmm0,xmm4; ret
    std::memcpy(eboot_kit::at(0x2713d95),wind_return,sizeof wind_return);
    Writable(0x27149f2);
    constexpr u8 force_return[]{0xc4,0xc1,0x78,0x28,0xc6,0xc3}; // vmovaps xmm0,xmm14; ret
    std::memcpy(eboot_kit::at(0x27149f2),force_return,sizeof force_return);
    const auto wind_step = eboot_kit::fn<float (*)()>(0x2713d8d);
    const auto wind_force = eboot_kit::fn<float (*)()>(0x27149ea);
    if (!wind) {
        assert(std::memcmp(eboot_kit::at(0x27149ea),original_force.data(),original_force.size())==0);
    }
    for (float dt : {1.0f/60,1.0f/144,BbFrameRate::Thirtieth,1.0f/480,0.1f,0.0f,-1.0f,
                     std::numeric_limits<float>::quiet_NaN()}) {
        std::memcpy(pacer.data()+0x264,&dt,4);
        step(pacer.data());
        const float want = std::isfinite(dt) ? std::clamp(dt,1.0f/480,BbFrameRate::Thirtieth)
                                             : BbFrameRate::Thirtieth;
        assert(BbFrameRate::game_clock.Read().dt == want);
        assert(BbFrameRate::PacedFrame()==(std::isfinite(dt) && dt+0.000002f>=1.0f/480));
        // Enter the installed retail cloth hook across the initial diagnostic
        // sampling window and afterwards. Diagnostics must not change physics.
        const auto cloth = eboot_kit::fn<float (*)(const u8*,int,int,int,float,float)>(0x1567a80);
        alignas(16) std::array<u8,0x30> constraint{};
        for (int sample=0;sample<24000;++sample) {
            const u32 type=sample%12;
            std::memcpy(constraint.data()+0x18,&type,4);
            const int mode=sample%3, substeps=1+sample%4, iteration=sample%substeps;
            const float a=0.8f,b=1.2f;
            const float game=mode==2 ? std::pow(float(substeps)*a*b,load<float>(0x4cd7bbc)) : 1.0f;
            const float expected = mode==2 && type==8 ? float(iteration+1)/substeps :
                mode==2 && (type==5 || type==10) ? (iteration==substeps-1 ? 1.0f : 0.0f) :
                (type==5 || type==8 || type==10) ? game : game*want*30.0f;
            assert(std::abs(cloth(constraint.data(),mode,iteration,substeps,a,b)-expected)<1e-6f);
        }
        assert(wind_step() == (wind ? want*0.5f : 1.0f/60));
        if (wind) assert(wind_force() == 1.0f/60);
        assert(yebis(0,1.0f/60) == 0);
        const float elapsed = Load<float>(reinterpret_cast<u64>(effect.data())+0x6ac);
        assert(std::abs(elapsed - (native ? want/2 : 1.0f/60)) < 1e-8f);
        store(self+0x60,BbFrameRate::Thirtieth);
        drive(self);
        assert(driven == (native ? want : BbFrameRate::Thirtieth));
        assert(load<float>(self+0x60) == BbFrameRate::Thirtieth);
        store<float>(self+0x60,1.0f/90); // custom/Behavior drive steps must survive
        drive(self);
        assert(driven == 1.0f/90 && load<float>(self+0x60) == 1.0f/90);
        const auto* lens = eboot_kit::at(0x125d6e1);
        s32 disp;std::memcpy(&disp,lens+4,4);
        float rate;std::memcpy(&rate,lens+8+disp,4);
        assert(std::abs(rate - (native ? 1.0f/want : 30.0f)) < 1e-4f);
    }
    if (std::getenv("CACHYBORNE_CLOTH_BENCH")) {
        const auto cloth = eboot_kit::fn<float (*)(const u8*,int,int,int,float,float)>(0x1567a80);
        alignas(16) std::array<u8,0x30> constraint{};
        constexpr int count=10'000'000;
        for (int round=0;round<3;++round) {
            const auto begin=std::chrono::steady_clock::now();
            double checksum=0;
            for (int i=0;i<count;++i) checksum+=cloth(constraint.data(),1,0,1,1,1);
            const auto elapsed=std::chrono::steady_clock::now()-begin;
            std::printf("Cloth hook benchmark: %.3f ns/call checksum %.9g\n",
                std::chrono::duration<double,std::nano>(elapsed).count()/count,checksum);
        }
    }
    munmap(thunk,4096);
    std::puts(native ? "PASS: installed native clock hooks, real measured pacer delta, YEBIS/ragdoll/lens consumers"
                     : "PASS: native clock off keeps original YEBIS/ragdoll/lens behavior; canonical game time still publishes");
}
