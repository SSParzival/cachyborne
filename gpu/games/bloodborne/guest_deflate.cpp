// SPDX-License-Identifier: GPL-2.0-or-later
#include "guest_deflate.h"
#include <algorithm>
#include <cstring>
#include <libdeflate.h>
#include <memory>
#include <new>

namespace BbDeflate {
namespace {
constexpr std::size_t MaxScratch = 8u << 20;
struct Decoder {
    libdeflate_decompressor* value = libdeflate_alloc_decompressor();
    ~Decoder() { libdeflate_free_decompressor(value); }
};
bool Range(const void* ptr, std::size_t size) {
    const auto address = reinterpret_cast<std::uintptr_t>(ptr);
    return ptr && size <= UINTPTR_MAX - address;
}
bool Overlap(const void* a, std::size_t n, const void* b, std::size_t m) {
    const auto x = reinterpret_cast<std::uintptr_t>(a), y = reinterpret_cast<std::uintptr_t>(b);
    return x < y + m && y < x + n;
}
}
bool TryInflate(Stream* stream, int flush, const Memory& memory) noexcept {
    if (!memory.mapped || !memory.commit || !Range(stream, sizeof(*stream)) ||
        !memory.mapped(stream, sizeof(*stream)) || (flush != 0 && flush != 4)) return false;
    const Stream original = *stream;
    auto* state = static_cast<StatePrefix*>(original.state);
    if (!Range(state, sizeof(*state)) || !memory.mapped(state, sizeof(*state)) ||
        original.total_in || original.total_out || state->mode != 0 || state->wrap != 1 ||
        state->wbits != 15 || state->havedict || state->hold || state->bits ||
        original.avail_in < 6 || original.avail_in > MaxScratch ||
        !original.avail_out || original.avail_out > MaxScratch ||
        !Range(original.next_in, original.avail_in) || !Range(original.next_out, original.avail_out) ||
        Overlap(original.next_in, original.avail_in, original.next_out, original.avail_out) ||
        Overlap(stream, sizeof(*stream), original.next_out, original.avail_out) ||
        Overlap(state, sizeof(*state), original.next_out, original.avail_out) ||
        !memory.mapped(original.next_in, original.avail_in) ||
        !memory.mapped(original.next_out, original.avail_out)) return false;
    // The fast path excludes dictionaries, gzip, raw streams, partial streams,
    // nonstandard windows and block/sync-flush requests. None alter guest state.
    const unsigned header = unsigned(original.next_in[0]) * 256 + original.next_in[1];
    if (original.next_in[0] != 0x78 ||
        header % 31 || (original.next_in[1] & 32)) return false;
    thread_local Decoder decoder;
    if (!decoder.value) return false;
    // libdeflate may modify output on failure. A per-call bounded scratch buffer
    // makes fallback transactional, including corrupt input and short output.
    std::unique_ptr<std::uint8_t[]> scratch(new (std::nothrow) std::uint8_t[original.avail_out]);
    if (!scratch) return false;
    std::size_t consumed = 0, produced = 0;
    if (libdeflate_zlib_decompress_ex(decoder.value, original.next_in, original.avail_in,
                                     scratch.get(), original.avail_out, &consumed, &produced) !=
        LIBDEFLATE_SUCCESS) return false;
    const auto checksum = libdeflate_adler32(1, scratch.get(), produced);
    if (!memory.commit(original.next_out, scratch.get(), produced)) return false;
    stream->next_in += consumed;
    stream->avail_in -= std::uint32_t(consumed);
    stream->total_in = consumed;
    stream->next_out += produced;
    stream->avail_out -= std::uint32_t(produced);
    stream->total_out = produced;
    stream->adler = checksum;
    stream->data_type = 64; // final block, zero buffered bits, not a block-boundary return
    state->mode = 26; // DONE in zlib 1.2.3 (NOT the host zlib's enum)
    state->last = 1;
    state->flags = 0;
    state->check = checksum;
    state->total = produced;
    return true;
}
} // namespace BbDeflate
