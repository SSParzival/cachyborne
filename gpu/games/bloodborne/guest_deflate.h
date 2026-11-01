// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>

namespace BbDeflate {
// LP64 z_stream and the prefix of inflate_state in the verified 1.09 zlib 1.2.3.
// This is deliberately independent of the host zlib's private state layout.
struct Stream {
    const std::uint8_t* next_in;
    std::uint32_t avail_in, pad_in;
    std::uint64_t total_in;
    std::uint8_t* next_out;
    std::uint32_t avail_out, pad_out;
    std::uint64_t total_out;
    const char* msg;
    void* state;
    void* (*alloc)(void*, unsigned, unsigned);
    void (*free)(void*, void*);
    void* opaque;
    int data_type, pad_type;
    std::uint64_t adler, reserved;
};
struct StatePrefix {
    int mode, last, wrap, havedict, flags;
    unsigned dmax;
    std::uint64_t check, total;
    void* head;
    unsigned wbits, wsize, whave, write;
    void* window;
    std::uint64_t hold;
    unsigned bits;
};
static_assert(sizeof(Stream) == 112 && offsetof(Stream, state) == 56);
static_assert(offsetof(StatePrefix, wbits) == 48 && offsetof(StatePrefix, bits) == 80);
struct Memory {
    bool (*mapped)(const void*, std::size_t);
    bool (*commit)(void*, const void*, std::size_t);
};
// Complete initial zlib streams only. Failure leaves stream, state and destination
// unchanged, allowing the caller to run the original guest implementation.
bool TryInflate(Stream* stream, int flush, const Memory& memory) noexcept;
} // namespace BbDeflate
