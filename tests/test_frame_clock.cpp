// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_frame_clock.h"
#include "bbport_clock_consumers.h"
#include <array>
#include <cassert>
#include <cmath>
#include <limits>
#include <thread>

namespace {
float seen;
std::uint64_t Step(std::uintptr_t key, std::uintptr_t bodies, std::uintptr_t palette, std::uintptr_t indices) {
    assert(key == 1 && palette == 3 && indices == 4);
    std::memcpy(&seen, reinterpret_cast<const void*>(bodies + 0x10), sizeof seen);
    return 42;
}
}
int main() {
    using namespace BbFrameRate;
    for (int rate : {30,60,90,120,144,165,240,360,480}) {
        FrameClock clock;
        double seconds = 0;
        int ticks = 0;
        for (int i = 0; i < rate * 60; ++i) {
            const float dt = i % 3 == 0 ? 0.9f / rate : i % 3 == 1 ? 1.1f / rate : 1.0f / rate;
            clock.Publish(dt);
            const auto frame = clock.Read();
            seconds += std::min(dt,Thirtieth);
            ticks += frame.tick30;
            assert(std::abs(ticks - seconds * 30) < 1.0001);
            assert(std::abs(YebisElapsed(1.0f/60,frame) - frame.dt/2) < 1e-8f);
        }
    }
    FrameClock clock;
    assert(clock.Read().dt == Thirtieth && clock.Read().tick30);
    for (float dt : {0.0f,-1.0f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
        clock.Publish(dt);
        assert(clock.Read().dt == 0 && !clock.Read().tick30);
    }
    clock.Publish(10);
    assert(clock.Read().dt == Thirtieth && clock.Read().tick30);
    // A reader sees a single published pair, including under concurrent reads.
    std::atomic<bool> stop{false};
    std::jthread reader([&] {
        while (!stop.load()) {
            const auto frame = clock.Read();
            assert((frame.dt == Thirtieth && frame.tick30) || (frame.dt == 0 && !frame.tick30));
        }
    });
    for (int i = 0; i < 100000; ++i) clock.Publish(i & 1 ? Thirtieth : 0);
    stop = true;
    reader.join();
    std::array<unsigned char,64> data{};
    for (float value : {Thirtieth,1.0f/60,0.02f,0.0f,-0.0f,std::numeric_limits<float>::quiet_NaN()}) {
        std::memcpy(data.data()+0x10,&value,4);
        const auto bits = std::bit_cast<std::uint32_t>(value);
        assert(DriveKeyFrame(1,reinterpret_cast<std::uintptr_t>(data.data()),3,4,{1.0f/144,true},Step) == 42);
        assert(std::bit_cast<std::uint32_t>(seen) == (value == Thirtieth ? std::bit_cast<std::uint32_t>(1.0f/144) : bits));
        float restored;
        std::memcpy(&restored,data.data()+0x10,4);
        assert(std::bit_cast<std::uint32_t>(restored) == bits);
    }
    assert(std::bit_cast<std::uint32_t>(YebisElapsed(1.0f/60,{})) == std::bit_cast<std::uint32_t>(1.0f/60));
}
