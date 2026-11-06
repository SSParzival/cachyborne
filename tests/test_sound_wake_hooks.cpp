// SPDX-License-Identifier: GPL-2.0-or-later
#include "retail_timing_kit.h"
#include "shim/bbport_sound_wake.h"
#include "shim/bbport_guest_code_hooks.h"
#include "shim/bbport_guest_code.h"
#include <thread>

namespace {
unsigned acceptance_calls=0;
void Acceptance(u64,const Core::GuestRegisterSnapshot*) { ++acceptance_calls; }
void Writable(u64 at) {
    assert(mprotect(reinterpret_cast<void*>(at&~u64(4095)),4096,
                    PROT_READ|PROT_WRITE|PROT_EXEC)==0);
}
}
int main() {
    if (!eboot_kit::load()) return 77;
    const auto call = load<u64>(0x2c0137d);
    setenv("BB_SOUND_WAKE","0",1);
    assert(!BbSoundWake::Install(eboot_kit::image,0x6000000));
    assert(load<u64>(0x2c0137d)==call && !BbSoundWake::Enabled());
    unsetenv("BB_SOUND_WAKE");
    assert(!BbSoundWake::Install(eboot_kit::image,32));
    // Refuse wrong producer identity, timeout/config and call target; no
    // partial observer install must prevent the subsequent valid install.
    for (const u64 site : {0x2c01373ull,0x2c0137eull,0x4b33c51ull,0x2434776ull}) {
        const u8 original=load<u8>(site);
        store<u8>(site,original^1);
        assert(!BbSoundWake::Install(eboot_kit::image,0x6000000));
        assert(!BbSoundWake::Enabled());
        store(site,original);
    }
    // Acceptance already uses the first six pacer bytes. Both observers must
    // coexist and run once for the same real guest call.
    constexpr std::array<u8,6> entry{0x55,0x48,0x89,0xe5,0x41,0x57};
    assert(Core::InstallGuestCodeHook(eboot_kit::at(0x2434770),entry,0,Acceptance));
    assert(BbSoundWake::Install(eboot_kit::image,0x6000000));
    assert(BbSoundWake::Install(eboot_kit::image,0x6000000));
    assert(BbSoundWake::Enabled());
    assert(std::memcmp(eboot_kit::at(0x4b33c51),"33333",5)==0);
    assert(load<u32>(0x25629f1)==33000);
    // Replace only the unexecuted continuation: the byte-checked prologue and
    // both installed observers execute unchanged, then unwind their real pushes.
    Writable(0x2434781);
    constexpr u8 unwind[]{0x48,0x83,0xc4,0x38,0x5b,0x41,0x5c,0x41,0x5d,0x41,0x5e,0x41,0x5f,0x5d,0xc3};
    std::memcpy(eboot_kit::at(0x2434781),unwind,sizeof unwind);
    const auto frame=eboot_kit::fn<void (*)()>(0x2434770);
    // Execute the actual patched callsite with the original CALL alignment.
    Writable(0x2c01382);
    constexpr u8 finish[]{0x48,0x83,0xc4,0x08,0xc3};
    std::memcpy(eboot_kit::at(0x2c01382),finish,sizeof finish);
    BbGuestCode::Emitter thunk;
    thunk.Bytes({0x48,0x83,0xec,0x08});
    thunk.JmpAbs(0x2c0137d);
    auto* code=static_cast<u8*>(mmap(nullptr,4096,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
    assert(code!=MAP_FAILED);
    std::memcpy(code,thunk.code.data(),thunk.size);
    assert(mprotect(code,4096,PROT_READ|PROT_EXEC)==0);
    const auto sleep=reinterpret_cast<int (*)(u32)>(code);
    frame();
    assert(acceptance_calls==1 && BbSoundWake::Read().frames==1);
    assert(sleep(33333)==0); // pending generation: immediate pass
    assert(BbSoundWake::Read().timeouts==0);
    std::thread sound([&] { assert(sleep(100000)==0); });
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while (!BbSoundWake::Read().waiting) {
        assert(std::chrono::steady_clock::now()<end);
        std::this_thread::yield();
    }
    frame();
    sound.join();
    assert(acceptance_calls==2 && BbSoundWake::Read().frames==2);
    assert(BbSoundWake::Read().passes==2 && BbSoundWake::Read().timeouts==0);
    assert(sleep(1000)==0 && BbSoundWake::Read().timeouts==1);
    munmap(code,4096);
    std::puts("PASS: retail sound callsite, before-limiter observer, acceptance coexistence, pending/blocked wake and original timeouts");
}
