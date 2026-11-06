// SPDX-License-Identifier: GPL-2.0-or-later
// Actual retail entries and native trampolines, with stand-in update/task
// bodies. Full grid/impulse semantics are covered by water-gate-model-test.
#include "retail_timing_kit.h"
#include "games/bloodborne/water_gate.h"
#include <array>

namespace {
constexpr u64 Update = 0x2677780, Task = 0x2677e50;
alignas(16) std::array<u8,0x100> manager{};
alignas(16) std::array<u8,0x7900> gx{};
std::array<u64,2> params{reinterpret_cast<u64>(manager.data()),0};
unsigned updates = 0, tasks = 0;
u64 FakeUpdate(u64 self) {
    assert(self == reinterpret_cast<u64>(manager.data()));
    ++updates;
    return 0x7777;
}
u64 FakeTask(u64 self,u64 context,u64 param) {
    assert(self == 0x1234 && context == 0 && param == reinterpret_cast<u64>(params.data()));
    ++tasks;
    return 0x8888;
}
void Body(u64 address,u32 stack,void* function) {
    std::array<u8,32> body{0x48,0x81,0xc4}; // undo the retail prologue
    std::memcpy(body.data()+3,&stack,4);
    constexpr std::array<u8,16> restore{
        0x5b,0x41,0x5c,0x41,0x5d,0x41,0x5e,0x41,0x5f,0x5d,
        0xff,0x25,0,0,0,0};
    std::memcpy(body.data()+7,restore.data(),restore.size());
    std::memcpy(body.data()+23,&function,8);
    std::memcpy(eboot_kit::at(address+20),body.data(),31);
}
}

int main(int argc,char** argv) {
    if (!eboot_kit::load()) return 77;
    if (argc == 2) {
        const int which = std::atoi(argv[1]);
        assert(which == 0 || which == 1);
        *eboot_kit::at(which ? Task : Update) ^= 1;
        std::array<u8,20> before_update, before_task;
        std::memcpy(before_update.data(),eboot_kit::at(Update),20);
        std::memcpy(before_task.data(),eboot_kit::at(Task),20);
        assert(!BbWaterGate::Install(eboot_kit::image,0x6000000));
        assert(!std::memcmp(before_update.data(),eboot_kit::at(Update),20));
        assert(!std::memcmp(before_task.data(),eboot_kit::at(Task),20));
        return 0;
    }
    Body(Update,0x68,reinterpret_cast<void*>(&FakeUpdate));
    Body(Task,0x328,reinterpret_cast<void*>(&FakeTask));
    auto update = eboot_kit::fn<u64(*)(u64)>(Update);
    auto task = eboot_kit::fn<u64(*)(u64,u64,u64)>(Task);
    const auto m = reinterpret_cast<u64>(manager.data());
    const auto p = reinterpret_cast<u64>(params.data());
    assert(update(m) == 0x7777 && task(0x1234,0,p) == 0x8888);
    assert(!BbWaterGate::Install(nullptr,0x6000000));
    assert(!BbWaterGate::Install(eboot_kit::image,0x5540ddf));
    assert(BbWaterGate::Install(eboot_kit::image,0x6000000));
    assert(BbWaterGate::Install(eboot_kit::image,0x6000000));
    // No GX world: complete pass-through including the task's guest return.
    store<u64>(reinterpret_cast<u64>(eboot_kit::image)+0x5540dd8,0);
    assert(update(m) == 0x7777 && task(0x1234,0,p) == 0x8888);
    store<u64>(reinterpret_cast<u64>(eboot_kit::image)+0x5540dd8,reinterpret_cast<u64>(gx.data()));
    // First gated update starts, the same game time holds, then a whole step
    // advances. No grids or impulses exist in this entry/trampoline fixture.
    assert(update(m) == 0x7777 && task(0x1234,0,p) == 0x8888);
    assert(update(m) == 0x7777 && task(0x1234,0,p) == 0);
    store<float>(reinterpret_cast<u64>(gx.data())+0x78cc,1.0f/30);
    assert(update(m) == 0x7777 && task(0x1234,0,p) == 0x8888);
    const auto stats = BbWaterGate::Read();
    assert(updates == 5 && tasks == 4);
    assert(stats.frames == 3 && stats.steps == 2 && stats.held == 1);
    std::puts("PASS: retail entries, original trampolines, native ABI/returns, time hold/step");
}
