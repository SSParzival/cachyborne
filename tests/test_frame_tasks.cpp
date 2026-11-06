// SPDX-License-Identifier: GPL-2.0-or-later
#include "retail_timing_kit.h"
#include "shim/bbport_frame_tasks.h"
#include "shim/bbport_sound_wake.h"
#include <atomic>
#include <thread>

namespace {
std::array<std::atomic<unsigned>,17> calls{};
template<unsigned N> void Task() { ++calls[N]; }
constexpr std::array<BbFrameTasks::Task,17> tasks{
    Task<0>,Task<1>,Task<2>,Task<3>,Task<4>,Task<5>,Task<6>,Task<7>,Task<8>,
    Task<9>,Task<10>,Task<11>,Task<12>,Task<13>,Task<14>,Task<15>,Task<16>};
}
int main() {
    if (!eboot_kit::load()) return 77;
    auto* image=eboot_kit::image;
    assert(!BbFrameTasks::Subscribe(image,32,tasks[0]));
    assert(!BbFrameTasks::Subscribe(image,0x6000000,nullptr));
    auto* entry=eboot_kit::at(0x2434770);
    entry[0]^=1;
    assert(!BbFrameTasks::Subscribe(image,0x6000000,tasks[0]));
    entry[0]^=1;
    assert(BbFrameTasks::Subscribe(image,0x6000000,tasks[0]));
    assert(BbFrameTasks::Subscribe(image,0x6000000,tasks[0]));
    assert(!BbFrameTasks::Subscribe(image+16,0x6000000,tasks[1]));
    // The sound observer occupies the immediately following prologue bytes.
    unsetenv("BB_SOUND_WAKE");unsetenv("BB_SOUND_WAKE_LOG");
    assert(BbSoundWake::Install(image,0x6000000));
    assert(mprotect(reinterpret_cast<void*>(0x2434000),4096,
                    PROT_READ|PROT_WRITE|PROT_EXEC)==0);
    constexpr u8 unwind[]{0x48,0x83,0xc4,0x38,0x5b,0x41,0x5c,0x41,0x5d,
                          0x41,0x5e,0x41,0x5f,0x5d,0xc3};
    std::memcpy(eboot_kit::at(0x2434781),unwind,sizeof unwind);
    const auto frame=eboot_kit::fn<void (*)()>(0x2434770);
    frame();
    assert(calls[0]==1 && BbSoundWake::Read().frames==1);
    // Publish later callbacks while the first already runs: the dispatch sees
    // either the old immutable prefix or the newly published complete one.
    std::thread guest([&] { for (unsigned i=0;i<1000;++i) frame(); });
    for (unsigned i=1;i<16;++i) assert(BbFrameTasks::Subscribe(image,0x6000000,tasks[i]));
    guest.join();
    assert(calls[0]==1001 && BbSoundWake::Read().frames==1001);
    assert(!BbFrameTasks::Subscribe(image,0x6000000,tasks[16]));
    assert(BbFrameTasks::Subscribe(image,0x6000000,tasks[0])); // dedup even when full
    std::array<unsigned,16> before;
    for (unsigned i=0;i<16;++i) before[i]=calls[i].load();
    frame();
    for (unsigned i=0;i<16;++i) assert(calls[i]==before[i]+1);
    assert(calls[16]==0 && BbSoundWake::Read().frames==1002);
    std::puts("PASS: real pacer observer, sound coexistence, concurrent task publication, dedup and capacity");
}
