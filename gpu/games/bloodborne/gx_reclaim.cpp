// SPDX-License-Identifier: GPL-3.0-or-later
// BBHost's block_reclaim/rename_wait utility, adapted to our Vulkan producer.
// Observe the exact second-copy result: a reclaim count alone also includes
// recovery followed by table failure, which cannot safely repeat the rename.
#include "games/bloodborne/gx_reclaim.h"
#include "platform/gnm/gnm.h"
#include "shim/bbport_guest_code_hooks.h"
#include "shim/bbport_toggles.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

namespace BbGxReclaim {
namespace {
using Clock = std::chrono::steady_clock;
using Ns = std::chrono::nanoseconds;
constexpr std::uint64_t Rename = 0x26d0d30, CopyResult = 0x26d0da9, Collector = 0x26aa860;
constexpr std::uint64_t TableEmptyCaller = 0x26ade27;
constexpr std::array<unsigned char,17> RenameEntry{
    0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec,0x38};
constexpr std::array<unsigned char,8> ResultEntry{0x48,0x89,0xc1,0xb8,0x09,0x00,0x00,0x00};
// Validate the frame slots, allocator register, alignment and original reclaim
// call that the slow-path observer reuses, not only the two patched instructions.
constexpr std::array<unsigned char,36> CopyCall{
    0x4c,0x89,0x75,0xc8,0x49,0x8b,0x7c,0x24,0x08,0x48,0x8d,0x75,0xc8,
    0xe8,0xc9,0x9a,0xfd,0xff,0x49,0x8b,0x04,0x24,0xba,0x10,0x00,0x00,0x00,
    0x4c,0x89,0xe7,0x8b,0x75,0xac,0xff,0x50,0x18};
constexpr std::array<unsigned char,5> TableReclaimCall{0xe8,0x39,0xca,0xff,0xff};
constexpr std::array<unsigned char,7> PoolCounter{0x48,0xff,0x89,0xc8,0x00,0x00,0x00};
constexpr std::array<unsigned char,20> CollectorEntry{
    0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x81,0xe4,0xe0,0xff,0xff,0xff};
struct State {
    std::mutex mutex;
    unsigned char* image = nullptr;
    bool result_hook = false;
    std::atomic<bool> collector_hook{false}, installed{false};
    bool waiting = true;
    Progress progress = nullptr;
    void* collector_original = nullptr;
    std::atomic<std::uint64_t> copy_slow_paths{0}, copy_failures{0}, recovered{0}, exhausted{0};
    std::atomic<std::uint64_t> reclaims{0}, pool_waits{0}, pool_recovered{0}, steps{0}, waited_ns{0};
};
State& Get() { static auto* const state = new State; return *state; }
void Report() {
    const auto s = Read();
    std::printf("GX allocation recovery: %llu reclaims, %llu copy slow paths, %llu copy failures, "
                "%llu recovered, %llu exhausted; %llu pool waits, %llu pool recoveries, "
                "%llu GPU steps, %.3f ms waited\n",
        static_cast<unsigned long long>(s.reclaims),static_cast<unsigned long long>(s.copy_slow_paths),
        static_cast<unsigned long long>(s.copy_failures),static_cast<unsigned long long>(s.recovered),
        static_cast<unsigned long long>(s.exhausted),static_cast<unsigned long long>(s.pool_waits),
        static_cast<unsigned long long>(s.pool_recovered),static_cast<unsigned long long>(s.steps),
        static_cast<double>(s.waited_ns)/1000000.0);
}
template<class Retry> void CatchUp(State& state, Retry&& retry) {
    const auto start = Clock::now(), step_deadline = start + std::chrono::milliseconds(200);
    const auto deadline = start + std::chrono::milliseconds(400);
    const auto advance = [&](unsigned stage, Ns cap) {
        const auto left = std::chrono::duration_cast<Ns>(deadline - Clock::now());
        return left > Ns::zero() && state.progress(stage, static_cast<std::uint64_t>(std::min(left, cap).count()));
    };
    unsigned steps = 0;
    bool finished = false;
    while (steps < 64 && Clock::now() < step_deadline && advance(0, std::chrono::milliseconds(50))) {
        ++steps;
        if (retry()) { finished = true; break; }
    }
    if (!finished && advance(1, std::chrono::milliseconds(50))) finished = retry();
    if (!finished && advance(2, std::chrono::milliseconds(150))) retry();
    state.steps.fetch_add(steps, std::memory_order_relaxed);
    state.waited_ns.fetch_add(std::chrono::duration_cast<Ns>(Clock::now() - start).count(), std::memory_order_relaxed);
}

void __attribute__((sysv_abi)) ObserveCopy(std::uint64_t, const Core::GuestRegisterSnapshot* regs) {
    auto& state = Get();
    state.copy_slow_paths.fetch_add(1,std::memory_order_relaxed);
    if (regs->rax) return; // the original reclaim already recovered a copy
    state.copy_failures.fetch_add(1,std::memory_order_relaxed);
    if (!state.waiting) return;
    // Resume INSIDE the original rename. Retry only its failed copy allocator,
    // never its table allocation or the entire rename. Normal successful Maps
    // do not enter this observer at all.
    using Collect = std::uint64_t(__attribute__((sysv_abi)) *)(std::uint64_t,std::uint64_t);
    using Allocate = std::uint64_t(__attribute__((sysv_abi)) *)(std::uint64_t,std::uint32_t,std::uint32_t);
    std::uint64_t pool, vtable, allocate;
    std::uint32_t bytes;
    std::memcpy(&pool,reinterpret_cast<void*>(regs->r12+8),8);
    std::memcpy(&vtable,reinterpret_cast<void*>(regs->r12),8);
    std::memcpy(&allocate,reinterpret_cast<void*>(vtable+0x18),8);
    std::memcpy(&bytes,reinterpret_cast<void*>(regs->rbp-0x54),4);
    std::uint64_t copy = 0;
    CatchUp(state,[&] {
        // The same context callback the original slow path supplied.
        reinterpret_cast<Collect>(state.collector_original)(pool,regs->rbp-0x38);
        copy = reinterpret_cast<Allocate>(allocate)(regs->r12,bytes,16);
        return copy != 0;
    });
    if (copy) {
        state.recovered.fetch_add(1,std::memory_order_relaxed);
        const_cast<Core::GuestRegisterSnapshot*>(regs)->rax = copy;
    } else state.exhausted.fetch_add(1,std::memory_order_relaxed);
}

void __attribute__((sysv_abi)) CollectorHook(std::uint64_t, const Core::GuestRegisterSnapshot* regs) {
    auto& state = Get();
    state.reclaims.fetch_add(1,std::memory_order_relaxed);
    if (BbStats::enabled) BbStats::NoteRangeCollector(regs->rdi);
    using Fn = std::uint64_t(__attribute__((sysv_abi)) *)(std::uint64_t,std::uint64_t);
    std::uint64_t before, caller;
    std::memcpy(&before,reinterpret_cast<void*>(regs->rdi+0xc8),8);
    // This is the live stack of the function entry, including host-created GX
    // stacks; it need not belong to the guest mapping registry.
    std::memcpy(&caller,reinterpret_cast<void*>(regs->rsp),8);
    std::uint64_t result;
    const auto run = [&] {
        result = reinterpret_cast<Fn>(state.collector_original)(regs->rdi,regs->rsi);
        std::uint64_t after;
        std::memcpy(&after,reinterpret_cast<void*>(regs->rdi+0xc8),8);
        return after != before;
    };
    const bool freed = run();
    // The copy slow-path observer owns its budget. Only the table-empty
    // allocation path needs a separate reclaim wait here.
    if (!freed && state.waiting && caller == reinterpret_cast<std::uint64_t>(state.image)+TableEmptyCaller) {
        state.pool_waits.fetch_add(1,std::memory_order_relaxed);
        bool recovered = false;
        CatchUp(state,[&] { recovered = run(); return recovered; });
        if (recovered) state.pool_recovered.fetch_add(1,std::memory_order_relaxed);
    }
    const_cast<Core::GuestRegisterSnapshot*>(regs)->rax = result;
}
}

bool Install(unsigned char* image,std::uint64_t size,Progress progress) {
    if (!image || size < CopyResult+ResultEntry.size()) return false;
    auto& state = Get();
    std::lock_guard lock{state.mutex};
    if (state.image && state.image != image) return false;
    if (state.installed.load(std::memory_order_acquire)) return true;
    if (std::memcmp(image+Rename,RenameEntry.data(),RenameEntry.size()) ||
        std::memcmp(image+0x26d0d85,CopyCall.data(),CopyCall.size()) ||
        std::memcmp(image+TableEmptyCaller-5,TableReclaimCall.data(),TableReclaimCall.size()) ||
        std::memcmp(image+0x26aa98b,PoolCounter.data(),PoolCounter.size()) ||
        (!state.result_hook && std::memcmp(image+CopyResult,ResultEntry.data(),ResultEntry.size())) ||
        (!state.collector_hook.load() && std::memcmp(image+Collector,CollectorEntry.data(),CollectorEntry.size()))) return false;
    state.image = image;
    state.progress = progress ? progress : &BbGnm::AdvanceGpuFor;
    state.waiting = !bb_env_off("BB_GX_RENAME_WAIT");
    if (!state.collector_hook.load()) {
        if (!Core::InstallGuestCodeHook(image+Collector,CollectorEntry,0,CollectorHook,nullptr,true,&state.collector_original)) return false;
        state.collector_hook.store(true,std::memory_order_release);
    }
    if (!state.result_hook) {
        if (!Core::InstallGuestCodeHook(image+CopyResult,ResultEntry,0,ObserveCopy)) return false;
        state.result_hook = true;
    }
    state.installed.store(true,std::memory_order_release);
    std::atexit(Report);
    std::printf("GX allocation recovery: exact copy failure, bounded GPU/label progress (%s; BB_GX_RENAME_WAIT=0 disables waits)\n",state.waiting ? "on" : "count only");
    return true;
}
bool CollectorInstalled() { return Get().collector_hook.load(std::memory_order_acquire); }
Stats Read() {
    auto& s=Get();
    return {s.installed.load(std::memory_order_acquire),s.waiting,s.copy_slow_paths.load(),s.copy_failures.load(),s.recovered.load(),s.exhausted.load(),s.reclaims.load(),s.pool_waits.load(),s.pool_recovered.load(),s.steps.load(),s.waited_ns.load()};
}
}
