// SPDX-FileCopyrightText: Copyright 2026 bbport contributors
// SPDX-License-Identifier: GPL-2.0-or-later
// Standalone CPU regression checks: no game, Vulkan device or window required.
#include <array>
#include <cassert>
#include <cstdio>
#include <numeric>
#include "gpu/upscale/motion_history.h"

using namespace Vulkan::Motion;

int main() {
    // Pera's material bias and camera cuts: no history across discontinuous cameras.
    assert(std::abs(UpscalerMipBias(1280, 1920, -1.0f) + 1.5849625f) < 1e-5f);
    assert(UpscalerMipBias(1920, 1920, -1.0f) == -1.0f);
    assert(UpscalerMipBias(960, 1920, -1.0f) == -2.0f);
    assert(UpscalerMipBias(1280, 1920, 0.0f) > -1.0f); // Other backends retain ratio bias.
    assert(UpscalerMipBias(0, 1920, -1.0f) == 0.0f);
    const std::array<float, 12> identity{1,0,0,0,0,1,0,0,0,0,1,0};
    auto moved = identity;
    moved[3] = 0.1f;
    assert(!IsCameraCut(MeasureCameraStep(identity, moved), 2.0f, 45.0f));
    moved[3] = 3.0f;
    assert(IsCameraCut(MeasureCameraStep(identity, moved), 2.0f, 45.0f));
    auto turned = identity;
    turned[2] = 1.0f; turned[10] = 0.0f;
    assert(IsCameraCut(MeasureCameraStep(identity, turned), 2.0f, 45.0f));
    moved[3] = std::numeric_limits<float>::quiet_NaN();
    assert(IsCameraCut(MeasureCameraStep(identity, moved), 2.0f, 45.0f));
    const std::array<uint16_t, 5> indices{1000, 1007, 1002, 1007, UINT16_MAX};
    const auto range = IndexedRange(std::span(indices), -900, true);
    assert(range.first == 100 && range.count == 8);
    assert(IndexedRange(std::span(indices), -1001, true).count == 0);
    assert(IndexedRange(std::span(indices), 0, false).count == 64536);
    const std::array<uint32_t, 3> large{100000, 100003, UINT32_MAX};
    assert(IndexedRange(std::span(large), 7, true).count == 4);
    assert(IndexedRange(std::span(large), INT32_MAX, true).count == 0);
    const std::array<uint16_t, 1> restart{UINT16_MAX};
    assert(IndexedRange(std::span(restart), 0, true).count == 0);
    assert(IndexedRange(std::span<const uint16_t>{}, 0, false).count == 0);

    Draw draw{.shader = 1, .geometry = 2, .indices = 3, .topology = 4,
              .index_count = 5, .instances = 2, .first_instance = 100,
              .vertices = range};
    History history(32);
    history.NextFrame();
    const auto first = history.Prepare(draw);
    const auto duplicate = history.Prepare(draw);
    assert(first.store && !first.load && first.first_vertex == 100);
    assert(first.first_instance == 100 && first.instances == 2);
    assert(duplicate.store == first.store + 16); // shared geometry, different draw
    assert(history.Used() == 32); // firstInstance must NOT multiply the allocation
    assert(!history.Prepare(draw).store && history.stats.exhausted == 1);
    history.NextFrame();
    const auto next = history.Prepare(draw);
    assert(next.load == first.store && next.store != first.store);
    assert(history.Prepare(draw).load == duplicate.store);
    history.NextFrame(); // a missing frame invalidates the match
    history.NextFrame();
    assert(!history.Prepare(draw).load);

    // A changed index buffer may reference formerly unused holes inside the same min/max.
    // Do not accept that history merely because its addresses/counts stayed the same.
    history.NextFrame();
    auto changed = draw;
    changed.topology++;
    assert(!history.Prepare(changed).load);
    changed = draw;
    changed.first_instance++;
    assert(!history.Prepare(changed).load);
    history.NextFrame();
    changed = draw;
    changed.vertices.first++;
    assert(!history.Prepare(changed).load);

    // Foliage: another set of instances (culling) at the same count is another identity.
    history.NextFrame();
    changed = draw;
    changed.instance_data = 1;
    history.Prepare(changed);
    history.NextFrame();
    changed.instance_data = 2;
    assert(!history.Prepare(changed).load);
    // Repeated draws matched by order: a jump means another object, a step the same one.
    History anchored(64);
    anchored.NextFrame();
    anchored.Prepare(draw, {0, 0, 0, true});
    anchored.Prepare(draw, {5, 0, 0, true});
    anchored.NextFrame();
    assert(!anchored.Prepare(draw, {5, 0, 0, true}).load && anchored.stats.jumped == 1);
    assert(anchored.Prepare(draw, {5.1f, 0, 0, true}).load);

    History overflow(32);
    draw.vertices.first = 0;
    draw.vertices.count = INT32_MAX;
    draw.instances = UINT32_MAX;
    draw.first_instance = 0;
    assert(!overflow.Prepare(draw).store);
    assert(overflow.stats.exhausted == 1);
    draw.first_instance = 10;
    assert(!overflow.Prepare(draw).store && overflow.stats.invalid == 1);
    draw.first_instance = 0;
    draw.vertices.first = INT32_MAX;
    draw.vertices.count = 2;
    assert(!overflow.Prepare(draw).store && overflow.stats.invalid == 2);

    // Small skeletons: history only while the bone palette changes (weapon spinning).
    History gated(1024);
    const History::GateKey weapon{.shader = 7, .index_count = 3, .instances = 1};
    const auto frame = [&](uint64_t palette) {
        gated.NextFrame();
        return gated.Moving(weapon, palette);
    };
    assert(!gated.Moving(weapon, 1) && gated.stats.still == 1); // first sight
    assert(!frame(1));                    // same bones: camera motion
    assert(frame(2) && frame(3));         // spinning
    assert(!frame(3));                    // stopped
    gated.NextFrame();
    gated.NextFrame();
    assert(!gated.Moving(weapon, 4));     // after a skipped frame: no comparison
    assert(!gated.Moving(weapon, 5));     // second occurrence of the same draw
    History swapped(1024); // two weapons; the first is culled: the second takes its place
    swapped.NextFrame();
    swapped.Moving(weapon, 1, {0, 0, 0, true});
    swapped.Moving(weapon, 2, {9, 0, 0, true});
    swapped.NextFrame();
    assert(!swapped.Moving(weapon, 3, {9, 0, 0, true})); // another weapon: camera motion
    swapped.NextFrame();
    assert(swapped.Moving(weapon, 4, {9.1f, 0, 0, true})); // the same one, moving
    assert(ClassifyBuffer(864) == BufferRole::Other && ClassifyBuffer(416) == BufferRole::Other);
    assert(ClassifyBuffer(96) == BufferRole::SmallSkeleton &&
           ClassifyBuffer(384) == BufferRole::SmallSkeleton);
    assert(ClassifyBuffer(656) == BufferRole::Skeleton && ClassifyBuffer(64) == BufferRole::Other);

    // Index ranges: a cohort loaded together is revalidated across the whole
    // interval; no mesh stays stale longer than Revalidate frames.
    IndexRangeCache ranges;
    uint32_t scans = 0;
    const IndexRangeCache::Key mesh{.address = 0x1000, .count = 3, .index_size = 2};
    const auto scan = [&] {
        ++scans;
        return IndexRangeCache::Result{{4, 3}, 99};
    };
    assert(ranges.Get(mesh, 0, scan).topology == 99 && scans == 1);
    for (uint64_t frame = 1; frame <= IndexRangeCache::Revalidate; ++frame)
        assert(ranges.Get(mesh, frame, scan).range == (VertexRange{4, 3}));
    assert(scans == 2);
    ranges.Trim(IndexRangeCache::Revalidate + IndexRangeCache::Unused + 1);
    assert(ranges.Size() == 0);

    IndexRangeCache cohort;
    constexpr size_t Meshes = 512;
    std::array<uint64_t, Meshes> last_scan{};
    std::array<uint32_t, IndexRangeCache::Revalidate> per_frame{};
    for (uint64_t frame = 0; frame <= 3 * IndexRangeCache::Revalidate; ++frame) {
        for (size_t i = 0; i < Meshes; ++i) {
            const IndexRangeCache::Key key{.address = 0x100000 + i * 0x10000,
                                          .count = 300, .index_size = 2};
            const auto result = cohort.Get(key, frame, [&] {
                assert(frame - last_scan[i] <= IndexRangeCache::Revalidate);
                last_scan[i] = frame;
                if (frame && frame <= IndexRangeCache::Revalidate) ++per_frame[frame-1];
                return IndexRangeCache::Result{{4, 3}, frame};
            });
            assert(frame - result.topology < IndexRangeCache::Revalidate);
        }
    }
    assert(std::accumulate(per_frame.begin(), per_frame.end(), 0u) == Meshes);
    assert(*std::max_element(per_frame.begin(), per_frame.end()) < Meshes / 8);

    // The range and topology hash must describe exactly one snapshot for both
    // index widths, including restart markers and signed base-vertex rejection.
    const auto check_snapshot = []<class Index>(std::vector<Index> input) {
        const auto hash = [&](const void* data, size_t bytes) {
            assert(data != input.data());
            uint64_t value = 0;
            for (size_t i = 0; i < bytes; ++i)
                value = value * 131 + static_cast<const unsigned char*>(data)[i];
            return value;
        };
        for (bool restart : {false, true}) {
            for (int base : {-10, 0, 20}) {
                const auto result = SnapshotIndexedRange(input.data(), input.size(), base, restart, hash);
                assert(result.first == IndexedRange(std::span<const Index>(input), base, restart));
                uint64_t expected = 0;
                for (size_t i = 0; i < input.size()*sizeof(Index); ++i)
                    expected = expected * 131 + reinterpret_cast<const unsigned char*>(input.data())[i];
                assert(result.second == expected);
            }
        }
    };
    check_snapshot(std::vector<uint16_t>{5, 8, 7, UINT16_MAX});
    check_snapshot(std::vector<uint32_t>{5, 8, 7, UINT32_MAX});

    assert(JitterPhases(1920, 1920) == 8);
    assert(JitterPhases(1280, 1920) == 18);
    assert(JitterPhases(960, 1920) == 32);
    assert(JitterPhases(640, 1920) == 72);
    assert(JitterPhases(0, 1920) == 8);
    std::puts("Motion history: PASS (ranges, restart, offsets, matching, instances, anchors, capacity, gating, index cache, jitter)");
}
