// SPDX-License-Identifier: GPL-2.0-or-later
#include "render/latency.h"
#include <cassert>
#include <thread>
#include <vector>
using namespace BbLatency;
int main() {
    Ring ring;
    Sample sample;
    auto id = ring.Begin(100, 200);
    assert(!ring.Take(id, sample));
    assert(!ring.Mark(0, Stage::QueueSubmit, 300));
    assert(!ring.Mark(id, Stage::Count, 300));
    assert(ring.Mark(id, Stage::QueueSubmit, 300));
    assert(ring.Mark(id, Stage::QueueSubmit, 301)); // keep first submit
    assert(ring.Mark(id, Stage::PresentReturned, 400));
    assert(!ring.Take(id, sample));
    assert(ring.Mark(id, Stage::GpuObserved, 450)); // observation can follow present return
    assert(ring.Take(id, sample));
    assert(sample.id == id && sample.ns[2] == 300 && sample.ns[3] == 450);
    assert(!ring.Take(id, sample)); // exactly once
    auto invalid = ring.Begin(1, 2);
    ring.Mark(invalid, Stage::QueueSubmit, 300);
    ring.Mark(invalid, Stage::GpuObserved, 299);
    ring.Mark(invalid, Stage::PresentReturned, 400);
    assert(!ring.Take(invalid, sample)); // reject impossible ordering
    for (unsigned i = 0; i < 128; ++i)
        ring.Begin(0, 0);
    assert(!ring.Mark(id, Stage::GpuObserved, 900)); // reject stale generation
    assert(!ring.Take(id, sample));
    assert(ring.Dropped() > 0);
    Ring concurrent;
    const auto shared = concurrent.Begin(1, 2);
    std::vector<std::jthread> writers;
    for (unsigned i = 0; i < 12; ++i)
        writers.emplace_back([&, i] {
            for (unsigned n = 0; n < 10000; ++n)
                concurrent.Mark(shared, Stage(2 + i % 3), 100 * (1 + i % 3));
        });
    writers.clear();
    // Contention may drop samples but cannot corrupt the ticket or wait on peers.
    concurrent.Mark(shared, Stage::QueueSubmit, 100);
    concurrent.Mark(shared, Stage::GpuObserved, 200);
    concurrent.Mark(shared, Stage::PresentReturned, 300);
    assert(concurrent.Take(shared, sample) && sample.id == shared);
    assert(!Enabled()); // no env init, clock, file or worker required
}
