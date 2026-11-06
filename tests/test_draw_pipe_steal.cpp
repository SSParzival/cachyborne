// SPDX-License-Identifier: GPL-2.0-or-later
#include "render/vk_draw_pipe.h"
#include <cassert>
#include <cstring>

namespace Vulkan {
// Stop only the empty recorder in deterministic pump tests. Queue operations
// and all competing-consumer stress cases use the actual production methods.
struct DrawPipeTestAccess {
    static void StopEmptyRecorder(DrawPipe& pipe) {
        assert(pipe.Idle());
        pipe.thread.request_stop();
        pipe.wake.fetch_add(1, std::memory_order_seq_cst);
        pipe.wake.notify_one();
        pipe.thread.join();
    }
};
}

namespace {
using Vulkan::DrawPipe;
struct Packet {
    u64 number, resolved;
    u32 size, needs_resolve;
};
struct Context {
    std::thread::id producer = std::this_thread::get_id();
    DrawPipe* pipe = nullptr;
    std::atomic<int> active{0};
    u64 expected = 1, helped = 0, worker = 0;
    std::atomic<u64> resolved{0};
    std::atomic<bool> hold_resolver{false}, resolver_entered{false};
};
u64 Marker(u64 n) { return n * 0x9e3779b97f4a7c15ull; }
void Consume(void* context, const u8* bytes, u32 size) {
    auto& c = *static_cast<Context*>(context);
    assert(c.active.fetch_add(1) == 0); // recording must never overlap
    Packet p;
    std::memcpy(&p, bytes, sizeof p);
    assert(p.number == c.expected++ && p.size == size);
    assert(c.pipe->RunningPacket() == p.number);
    assert(DrawPipe::OnStageB() && BbSections::recording_thread);
    if (std::this_thread::get_id() == c.producer) {
        assert(DrawPipe::Assisting());
        ++c.helped;
    } else {
        assert(!DrawPipe::Assisting());
        ++c.worker;
    }
    assert(p.resolved == (p.needs_resolve ? Marker(p.number) : 0));
    assert(bytes[size - 1] == u8(p.number)); // ring bytes were not overwritten early
    if (!(p.number % 7)) std::this_thread::yield();
    assert(c.active.fetch_sub(1) == 1);
}
void Resolve(void* context, u8* bytes, u32 size, u64 number, u32 worker) {
    auto& c = *static_cast<Context*>(context);
    Packet p;
    std::memcpy(&p, bytes, sizeof p);
    assert(p.number == number && p.size == size && worker < 3);
    if (c.hold_resolver.load()) {
        c.resolver_entered.store(true);
        c.resolver_entered.notify_one();
        while (c.hold_resolver.load()) c.hold_resolver.wait(true);
    }
    if (!(number % 5)) std::this_thread::yield();
    const u64 marker = Marker(number);
    std::memcpy(bytes + offsetof(Packet,resolved), &marker, sizeof marker);
    ++c.resolved;
}
void Post(DrawPipe& pipe, u32 size, bool resolve) {
    assert(size > sizeof(Packet));
    const u64 n = pipe.NextPacket();
    const Packet packet{n,0,size,u32(resolve)};
    auto* bytes = pipe.Begin(size);
    std::memcpy(bytes,&packet,sizeof packet);
    std::memset(bytes+sizeof packet,u8(n),size-sizeof packet);
    pipe.Commit(size,resolve);
}
void AssertCallerRole() {
    assert(!DrawPipe::OnStageB() && !DrawPipe::Assisting());
    assert(!BbSections::recording_thread && BbSections::command_thread);
}
void DeterministicPump() {
    unsetenv("BB_GX_BARRIER_STEAL");
    Context c;
    DrawPipe pipe(Consume,&c);
    c.pipe = &pipe;
    Vulkan::DrawPipeTestAccess::StopEmptyRecorder(pipe);
    Post(pipe,128,false);
    Post(pipe,192,false);
    const u64 second = pipe.NextPacket()-1;
    Post(pipe,256,false);
    pipe.WaitForPacket(second,DrawPipe::ReasonConstRam);
    assert(pipe.ConsumedPackets() == second); // do not over-drain the target
    assert(c.helped == 2 && c.worker == 0);
    AssertCallerRole();
    pipe.Drain();
    assert(c.helped == 3 && pipe.Idle());
    // More than four full rings without a worker: Begin's backpressure must
    // pump and handle wrap markers without losing or reordering a packet.
    for (unsigned i=0;i<1200;++i) Post(pipe,65536 + (i%3)*64,false);
    assert(c.helped > 3); // producer made progress before the explicit drain
    pipe.Drain();
    assert(pipe.ConsumedPackets() == 1203 && c.expected == 1204);
    assert(pipe.assisted_packets == c.helped && c.worker == 0);
    AssertCallerRole();
}
void PendingResolve() {
    Context c;
    c.hold_resolver = true;
    DrawPipe pipe(Consume,&c);
    c.pipe = &pipe;
    Vulkan::DrawPipeTestAccess::StopEmptyRecorder(pipe);
    pipe.StartResolvers(Resolve,3);
    Post(pipe,128,true);
    while (!c.resolver_entered.load()) c.resolver_entered.wait(false);
    // A resolver is deliberately pending. Neither consumer may record it or
    // keep the recording gate locked while waiting for the resolver to finish.
    std::jthread release([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        c.hold_resolver = false;
        c.hold_resolver.notify_all();
    });
    pipe.WaitForPacket(1,DrawPipe::ReasonConstRam);
    assert(c.helped == 1 && c.resolved == 1);
    AssertCallerRole();
}
void Stress(bool assistance, bool resolvers) {
    if (assistance) unsetenv("BB_GX_BARRIER_STEAL");
    else setenv("BB_GX_BARRIER_STEAL","0",1);
    Context c;
    {
        DrawPipe pipe(Consume,&c);
        c.pipe = &pipe;
        if (resolvers) pipe.StartResolvers(Resolve,3);
        u64 resolves = 0;
        for (unsigned i=0;i<30000;++i) {
            const bool resolve = resolvers && i%3 == 0;
            resolves += resolve;
            Post(pipe,64 + (i%53)*64,resolve);
            if (i%97 == 0) pipe.WaitForPacket(i+1,DrawPipe::ReasonConstRam);
            if (i%701 == 0) pipe.Drain(DrawPipe::ReasonSubmissionEnd);
            AssertCallerRole();
        }
        pipe.WaitResolved(30000);
        pipe.Drain();
        assert(c.expected == 30001 && c.helped+c.worker == 30000);
        assert(pipe.ConsumedPackets() == 30000 && pipe.Idle());
        assert(c.resolved == resolves);
        assert(pipe.assisted_packets == c.helped);
        if (!assistance) assert(c.helped == 0);
        // Go through the idle/sleep wakeup path after the pipe ran dry.
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        Post(pipe,128,false);
        pipe.Drain();
        assert(c.expected == 30002);
        // Destructor must drain a queued tail and stop/join all workers.
        for (unsigned i=0;i<17;++i) Post(pipe,192,false);
    }
    assert(c.expected == 30019 && c.active == 0);
    AssertCallerRole();
    std::printf("DrawPipe assistance %d, resolvers %d: %llu producer callbacks, %llu worker callbacks, ordered and exclusive\n",
                assistance,resolvers,(unsigned long long)c.helped,(unsigned long long)c.worker);
}
}
int main() {
    setenv("BB_PIPE_SPIN_US","0",1);
    BbSections::command_thread = true;
    DeterministicPump();
    PendingResolve();
    for (bool help : {false,true}) for (bool resolve : {false,true}) Stress(help,resolve);
    unsetenv("BB_GX_BARRIER_STEAL");
    std::puts("PASS: ordered DrawPipe assistance, target waits, wrap/backpressure, resolver readiness, wake and teardown");
}
