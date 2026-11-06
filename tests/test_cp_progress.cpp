// SPDX-License-Identifier: GPL-2.0-or-later
// Real CP queues: work completed later on another queue cannot satisfy a
// decode target while an earlier graphics submission remains blocked.
#include "cp/command_processor.h"
#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <thread>

namespace BbCp {
struct CommandProcessorTestAccess {
    static bool HasWaiter(CommandProcessor& cp) {
        std::scoped_lock lock{cp.submit_mutex};
        return cp.decoded_waiters != 0;
    }
    static u64 Decoded(CommandProcessor& cp,u32 queue) {
        std::scoped_lock lock{cp.submit_mutex};
        return cp.decoded_by_queue[queue];
    }
};
}

int main() {
    using namespace BbCp;
    using namespace std::chrono_literals;
    setenv("BB_STALL_REPORT","0",1);
    setenv("BB_GPU_SPIN_US","0",1);
    const std::array<u32,2> nop{(3u<<30)|(u32(Op::Nop)<<8),0};
    u32 ring_read=0;
    CommandProcessor cp;
    cp.SendCommand<true>([] {}); // CP thread id is published through the rendezvous
    const auto ring=cp.asc_queues.insert(reinterpret_cast<u64>(nop.data()),&ring_read,32u,0u);
    assert(ring.index==0); // compute vqid 1, as sceGnmMapComputeQueue registers it
    const auto until=[&](auto predicate) {
        const auto deadline=std::chrono::steady_clock::now()+2s;
        while (!predicate() && std::chrono::steady_clock::now()<deadline)
            std::this_thread::sleep_for(1ms);
        assert(predicate());
    };
    const std::array<u32,7> blocked{
        (3u<<30)|(5u<<16)|(u32(Op::WaitRegMem)<<8),
        3,0,0,1,0xffffffff,0x10}; // register 0 == 1, initially zero
    cp.SubmitGfx(blocked,{},true);
    bool accepted=true;
    std::thread waiter([&] { accepted=cp.WaitDecodedFor(200ms); });
    until([&] { return CommandProcessorTestAccess::HasWaiter(cp); });
    for (unsigned i=0;i<4;++i) cp.SubmitAsc(1,nop);
    until([&] { return CommandProcessorTestAccess::Decoded(cp,1)==4; });
    assert(CommandProcessorTestAccess::Decoded(cp,0)==0);
    waiter.join();
    assert(!accepted); // a global completed-count >= 1 would incorrectly pass
    cp.SendCommand<true>([&] { cp.regs.reg_array[0]=1; });
    assert(cp.WaitDecodedFor(1s));
    assert(CommandProcessorTestAccess::Decoded(cp,0)==1);
    cp.SendCommand<true>([&] { assert(!cp.WaitDecodedFor(1ns)); });
    std::puts("PASS: real independent CP queues, captured decode target, bounded timeout, release and producer-thread rejection");
}
