// SPDX-License-Identifier: GPL-2.0-or-later
// Regression for deferred data crossing the end of its command chunk.
#include "render/record_chunk.h"
#include <cassert>
#include <cstring>
#include <memory>

extern "C" { std::uint64_t runtime_disabled_optimizations = 0; }

int main() {
    auto chunk = std::make_unique<Vulkan::RecordChunk>();
    constexpr auto capacity = Vulkan::RecordChunk::Capacity;
    auto* first = static_cast<unsigned char*>(chunk->Allocate(capacity - 32, 1));
    std::memset(first, 0x37, capacity - 32);
    // This copy and its command exceed the inline arena. Both must remain
    // alive until execution, including an alignment greater than the arena's.
    auto* second = static_cast<unsigned char*>(chunk->Allocate(capacity + 29, 256));
    assert(second);
    assert(reinterpret_cast<std::uintptr_t>(second) % 256 == 0);
    std::memset(second, 0xa5, capacity + 29);
    unsigned calls = 0;
    auto owned = std::make_shared<unsigned>(123);
    std::weak_ptr<unsigned> lifetime = owned;
    assert(chunk->Push([&, first, second, owned](vk::CommandBuffer) {
        for (size_t i = 0; i < capacity - 32; ++i) assert(first[i] == 0x37);
        for (size_t i = 0; i < capacity + 29; ++i) assert(second[i] == 0xa5);
        assert(*owned == 123);
        ++calls;
    }));
    owned.reset();
    assert(!lifetime.expired() && chunk->Size() > capacity);
    chunk->Execute({});
    assert(calls == 1 && lifetime.expired() && chunk->Empty() && chunk->Size() == 0);

    // Reuse after execution; old captures and overflow blocks must be gone.
    for (unsigned i = 0; i < 12000; ++i) {
        assert(chunk->Push([&, i](vk::CommandBuffer) { assert(calls == i + 1); ++calls; }));
    }
    chunk->Execute({});
    assert(calls == 12001 && chunk->Size() == 0);
    auto pending = std::make_shared<unsigned>(7);
    std::weak_ptr<unsigned> cancelled = pending;
    assert(chunk->Push([pending](vk::CommandBuffer) { assert(false && "cancelled command ran"); }));
    pending.reset();
    chunk.reset();
    assert(cancelled.expired());

    Vulkan::RecordChunk invalid;
    for (size_t alignment : {size_t(0), size_t(3)}) {
        bool rejected = false;
        try { invalid.Allocate(8, alignment); }
        catch (const std::bad_array_new_length&) { rejected = true; }
        assert(rejected);
    }
    bool overflow = false;
    try { invalid.Allocate(SIZE_MAX, 64); }
    catch (const std::bad_array_new_length&) { overflow = true; }
    assert(overflow && invalid.Size() == 0);
    std::puts("PASS: deferred overflow, 256-byte alignment, data lifetime, ordered execution, reuse and cancellation");
}
