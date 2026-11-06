// SPDX-License-Identifier: GPL-3.0-or-later
// BBHOST engine/frame_rate.cpp, bdc5cdf8: preserve the Bloodletter L2's
// self-target frenzy hit when physics trails the pose above the original rate.
#include "games/bloodborne/bloodletter.h"
#include "common/guest_memory.h"
#include "shim/bbport_frame_tasks.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>

namespace BbBloodletter {
namespace {
constexpr std::uint32_t RowId=2900931, Original=0x3ecccccd, Corrected=0x3f19999a;
constexpr std::size_t RetailRowBytes=164;
struct State {
    std::mutex mutex;
    std::uintptr_t image=0;
    std::atomic<pid_t> owner{0};
    std::chrono::steady_clock::time_point last{};
};
State& Get() { static auto* const state=new State; return *state; }
bool Write(void*,BbParams::Address at,const void* data,std::size_t size) {
    return BbGuestMemory::Write(at,data,size);
}
void Tick() {
    auto& state=Get();
    std::uint8_t loading=1;
    if (!BbGuestMemory::Read(state.image+0x556286b,loading) || loading) return;
    const pid_t tid=gettid();pid_t zero=0;state.owner.compare_exchange_strong(zero,tid);
    if (state.owner.load()!=tid) return;
    const auto now=std::chrono::steady_clock::now();
    if (now-state.last<std::chrono::seconds(5)) return;
    state.last=now;
    const auto result=Correct({nullptr,BbGuestMemory::Read},Write,state.image+0x5540340);
    if (result==Result::Corrected)
        std::puts("Bloodletter self-hit: AtkParam_Pc row 2900931 hit0_Radius 0.4 -> 0.6 (converted frame rate)");
}
}

Result Correct(BbParams::Reader reader,Writer writer,BbParams::Address repository_slot) {
    const auto table=BbParams::FindTable(reader,repository_slot,"AtkParam_Pc");
    if (!table || table->type!="ATK_PARAM_ST") return Result::Unavailable;
    const auto row=BbParams::FindRow(reader,*table,RowId);
    if (!row || row->bytes!=RetailRowBytes || row->address%alignof(std::uint32_t))
        return Result::Unavailable;
    const auto radius=BbParams::ReadWord(reader,*row,0);
    if (!radius) return Result::Unavailable;
    if (*radius==Corrected) return Result::AlreadyCorrected;
    if (*radius!=Original) return Result::Custom;
    // One aligned word cannot straddle a page. A safe process_vm_writev
    // refusal therefore leaves it unchanged; no unchecked pointer store.
    if (!writer || !writer(reader.context,row->address,&Corrected,sizeof(Corrected)))
        return Result::WriteRefused;
    return Result::Corrected;
}

bool Install(unsigned char* image,std::uint64_t size) {
    if (!image || size<0x556286c) return false;
    auto& state=Get();std::lock_guard lock{state.mutex};
    if (state.image) return state.image==reinterpret_cast<std::uintptr_t>(image);
    state.image=reinterpret_cast<std::uintptr_t>(image);
    if (!BbFrameTasks::Subscribe(image,size,Tick)) { state.image=0;return false; }
    return true;
}
}
