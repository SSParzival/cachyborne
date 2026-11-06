// SPDX-License-Identifier: GPL-2.0-or-later
#include "platform/interrupts.h"
#include <array>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace {
std::atomic<unsigned> calls{0};
int owner;
void LateSignals() {
    std::thread signaler([] {
        for (int i=0;i<100;++i) BbIrq::Raise(BbIrq::Line::GfxEop);
        assert(calls.load()==101); // persistent listener + one queued callback
        BbIrq::Forget(BbIrq::Line::GfxEop,&owner);
        BbIrq::Raise(BbIrq::Line::GfxEop);
        assert(calls.load()==101);
    });
    signaler.join();
}
}

int main() {
    const pid_t child=fork();
    assert(child>=0);
    if (!child) {
        assert(std::atexit(LateSignals)==0);
        // Large captures require live callback storage, not just callable
        // pointer bits left behind in a destroyed std::function.
        std::array<unsigned,64> payload{};payload.back()=73;
        BbIrq::Listen(BbIrq::Line::GfxEop,&owner,[payload](BbIrq::Line line) {
            assert(line==BbIrq::Line::GfxEop && payload.back()==73);
            ++calls;
        });
        BbIrq::Once(BbIrq::Line::GfxEop,[payload](BbIrq::Line line) {
            assert(line==BbIrq::Line::GfxEop && payload.back()==73);
            ++calls;
        });
        std::exit(0);
    }
    int status=0;
    assert(waitpid(child,&status,0)==child);
    assert(WIFEXITED(status) && WEXITSTATUS(status)==0);
    std::puts("Interrupt line callbacks remain valid for late GPU signal threads");
}
