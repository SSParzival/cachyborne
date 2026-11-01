// SPDX-License-Identifier: GPL-2.0-or-later
#include "hooks_internal.h"
#include "guest_deflate.h"

extern "C" int runtime_memory_write_backing_rows(uintptr_t, const void*, uint64_t, uint64_t, uint64_t);
namespace BbGuestHooks {
namespace {
using InflateFn = int (PS4_SYSV_ABI *)(BbDeflate::Stream*, int);
InflateFn original_inflate = nullptr;
std::atomic<u64> accelerated{0}, original_calls{0}, output_bytes{0};
bool Mapped(const void* address, std::size_t size) {
    return size <= UINTPTR_MAX - reinterpret_cast<uintptr_t>(address) &&
           runtime_memory_is_mapped(reinterpret_cast<uintptr_t>(address), size);
}
bool Commit(void* dst, const void* src, std::size_t size) {
    if (!size) return true;
    runtime_memory_note_write(reinterpret_cast<uintptr_t>(dst), size);
    const bool ok = runtime_memory_write_backing_rows(reinterpret_cast<uintptr_t>(dst), src, size, size, 1);
    if (ok) runtime_memory_note_write(reinterpret_cast<uintptr_t>(dst), size);
    return ok;
}
int PS4_SYSV_ABI LoaderInflate(BbDeflate::Stream* stream, int flush) {
    if (BbDeflate::TryInflate(stream, flush, {Mapped, Commit})) {
        const u64 count = accelerated.fetch_add(1, std::memory_order_relaxed) + 1;
        const u64 bytes = output_bytes.fetch_add(stream->total_out, std::memory_order_relaxed) + stream->total_out;
        if (count == 1 || count == 32 || count % 4096 == 0)
            std::printf("Guest deflate: %llu streams, %llu KiB native; %llu fallback calls\n",
                        (unsigned long long)count, (unsigned long long)(bytes >> 10),
                        (unsigned long long)original_calls.load(std::memory_order_relaxed));
        return 1; // Z_STREAM_END
    }
    original_calls.fetch_add(1, std::memory_order_relaxed);
    return original_inflate(stream, flush);
}
}
bool InstallDecompression() {
    constexpr u64 offset = 0x2067ac0;
    // zlib 1.2.3 inflate(z_stream*, int), reached by the DCX stream reader
    // at 0x2625935. Whole instructions; the displaced prefix has no relative operands.
    constexpr std::array<u8, 20> expected = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57,
        0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x53, 0x48, 0x81, 0xec, 0x88, 0, 0, 0};
    auto* code = reinterpret_cast<u8*>(ImageBase + offset);
    if (std::memcmp(code, expected.data(), expected.size())) return false;
    Emitter fallback;
    for (std::size_t i = 0; i < 6; ++i) fallback.Bytes({expected[i]});
    fallback.JmpAbs(ImageBase + offset + 6);
    const u64 original = PlaceStub(fallback);
    Emitter redirect;
    redirect.JmpAbs(reinterpret_cast<u64>(&LoaderInflate));
    const u64 replacement = PlaceStub(redirect);
    u32 rel;
    if (!original || !replacement || !Rel32To(ImageBase + offset + 5, replacement, rel)) return false;
    const u64 page_size = u64(sysconf(_SC_PAGESIZE));
    void* page = reinterpret_cast<void*>((ImageBase + offset) & ~(page_size - 1));
    if (mprotect(page, page_size, PROT_READ | PROT_WRITE | PROT_EXEC)) return false;
    original_inflate = reinterpret_cast<InflateFn>(original);
    code[0] = 0xe9;
    std::memcpy(code + 1, &rel, sizeof(rel));
    code[5] = 0x90;
    mprotect(page, page_size, Core::GuestCodeProtection());
    return true;
}
} // namespace BbGuestHooks
