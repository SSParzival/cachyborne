// SPDX-License-Identifier: GPL-2.0-or-later
#include "retail_timing_kit.h"
#include "bbport_clock_consumers.h"
#include <array>

namespace {
struct alignas(16) Memory {
    std::array<u8,256> controller;
    std::array<u8,64> ragdoll;
};
Memory memory;
struct Seen {
    int calls = 0;
    u64 key = 0, bodies = 0, palette = 0, indices = 0;
    u32 step = 0;
    bool operator==(const Seen&) const = default;
};
Seen seen;
BbFrameRate::ClockFrame frame;
u64 Record(u64 key, u64 bodies, u64 palette, u64 indices) {
    ++seen.calls;
    seen.key = key; seen.bodies = bodies; seen.palette = palette; seen.indices = indices;
    seen.step = load<u32>(bodies + 0x10);
    store<u32>(bodies + 0x88, 0xc0ffee);
    return 42;
}
u64 Native(u64 key, u64 bodies, u64 palette, u64 indices) {
    return BbFrameRate::DriveKeyFrame(key,bodies,palette,indices,frame,Record);
}
}
int main() {
    if (!eboot_kit::load()) return 77;
    eboot_kit::Rng rng(0xae4600);
    rng.specials = true;
    using Drive = void (*)(u64);
    const auto game = eboot_kit::fn<Drive>(0xae4600);
    const auto self = reinterpret_cast<u64>(memory.controller.data());
    // The real drive keeps all its own tests/stores. Instrument only its
    // keyframe call, first as an oracle, then through the production helper.
    assert(load<u8>(0xae464d) == 0xe9 && load<u32>(0xae464e) == 0x553e);
    for (int i=0;i<20000;++i) {
        for (auto& b : memory.controller) b = u8(rng.u32());
        for (auto& b : memory.ragdoll) b = u8(rng.u32());
        store<float>(self+0x18,rng.value(-1,1));
        store<u64>(self+0x50,reinterpret_cast<u64>(memory.ragdoll.data()));
        store<u64>(reinterpret_cast<u64>(memory.ragdoll.data())+0x18,rng.chance(.5) ? 1 : 0);
        const float step = rng.chance(.5) ? BbFrameRate::Thirtieth : rng.value(-1,1);
        store(self+0x60,step);
        const auto before = memory;
        frame = {i % 3 ? rng.uniform(1.0f/480,1.0f/30) : BbFrameRate::Thirtieth,true};
        const bool convert = std::bit_cast<u32>(step) == 0x3d088889u && frame.dt != BbFrameRate::Thirtieth;
        if (convert) store(self+0x60,frame.dt);
        seen = {};
        eboot_kit::stub(0xae9b90,reinterpret_cast<const void*>(Record));
        game(self);
        if (convert) store(self+0x60,step);
        const auto expected = memory;
        const auto expected_seen = seen;
        memory = before;
        seen = {};
        eboot_kit::stub(0xae9b90,reinterpret_cast<const void*>(Native));
        game(self);
        assert(seen == expected_seen);
        assert(!std::memcmp(&memory,&expected,sizeof memory));
    }
    std::puts("PASS: 20000 original ragdoll drives, exact calls/memory; default step follows delta, custom steps preserved");
}
