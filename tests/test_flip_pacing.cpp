// SPDX-License-Identifier: GPL-2.0-or-later
#include "platform/videoout/flip_pacing.h"
#include <cassert>
#include <cstdio>

int main() {
    using namespace std::chrono;
    using namespace BbVideoOut::Pacing;
    const nanoseconds period{1'000'000'000/60};
    const auto start=Clock::time_point{};
    auto phase=start,relative=start;
    for (int i=0;i<10000;++i) {
        const auto wake=microseconds(50+i%80);
        phase=NextDeadline(phase,phase+wake,period,Policy::Phase);
        relative=NextDeadline(relative,relative+wake,period,Policy::Relative);
        assert(phase==start+(i+1)*period);
    }
    assert(relative-phase>milliseconds(800));
    const auto late=phase+milliseconds(4);
    assert(NextDeadline(phase,late,period,Policy::Phase)==late+period);
    assert(NextDeadline(phase,late,period,Policy::Relative)==late+period);
    assert(NextDeadline(phase,late,period,Policy::CatchUp)==phase+period);
    const auto very_late=phase+seconds(2);
    assert(NextDeadline(phase,very_late,period,Policy::Phase)==very_late+period);
    assert(NextDeadline(phase,very_late,period,Policy::CatchUp)==very_late-period);
    for (int fps : {30,60,90,120,144,240,480}) {
        const nanoseconds interval{1'000'000'000/fps};
        const auto small=std::min(interval/32,nanoseconds(50000));
        assert(NextDeadline(start,start+small,interval,Policy::Phase)==start+interval);
        assert(NextDeadline(start,start+interval,interval,Policy::Phase)==start+interval*2);
        assert(NextDeadline(start,start-small,interval,Policy::Phase)==start+interval);
    }
    assert(NextDeadline(start,start+seconds(1),nanoseconds(0),Policy::Phase)==start+seconds(1));
    std::puts("Flip pacing: bounded phase error, no accumulated wake drift, stall reset and legacy modes passed");
}
