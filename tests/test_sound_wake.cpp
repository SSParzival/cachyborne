// SPDX-License-Identifier: GPL-2.0-or-later
#include "shim/bbport_sound_wake.h"
#include <cassert>
#include <cstdio>
#include <thread>

using Clock = std::chrono::steady_clock;
using BbSoundWake::FrameWake;

int main() {
    FrameWake wake;
    // A frame published during the preceding sound pass must not be lost.
    for (int i=0;i<5;++i) wake.Frame();
    wake.Wait(100000);
    assert(wake.Read().passes==1 && wake.Read().timeouts==0);
    // Multiple generations coalesce into one pass; the next must really wait.
    std::thread sound([&] { wake.Wait(100000); });
    const auto deadline = Clock::now()+std::chrono::seconds(2);
    while (!wake.Read().waiting) {
        assert(Clock::now()<deadline);
        std::this_thread::yield();
    }
    wake.Frame();
    sound.join();
    assert(wake.Read().frames==6 && wake.Read().passes==2 && wake.Read().timeouts==0);
    // No notification: keep the original timeout, without hanging loading or
    // teardown. Zero, negative and corrupt oversized sleep requests are bounded.
    for (const int asked : {0,-1,1000,1000000}) {
        const auto before = Clock::now();
        wake.Wait(asked);
        assert(Clock::now()-before<std::chrono::seconds(2));
    }
    assert(wake.Read().timeouts==4 && !wake.Read().waiting);
    // Real producer/consumer handoff, including publication before Wait starts.
    FrameWake cadence;
    std::thread consumer([&] {
        for (unsigned pass=1;pass<=1000;++pass) cadence.Wait(100000);
    });
    for (unsigned frame=1;frame<=1000;++frame) {
        cadence.Frame();
        const auto until=Clock::now()+std::chrono::seconds(2);
        while (cadence.Read().consumed_frames<frame) {
            assert(Clock::now()<until);
            std::this_thread::yield();
        }
    }
    consumer.join();
    assert(cadence.Read().frames==1000 && cadence.Read().passes==1000 && cadence.Read().timeouts==0);
    std::puts("Sound wake: pending frames, coalescing, blocked wake and bounded timeouts OK");
}
