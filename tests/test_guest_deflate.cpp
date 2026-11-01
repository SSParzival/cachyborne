// SPDX-License-Identifier: GPL-2.0-or-later
#include "games/bloodborne/guest_deflate.h"
#include <zlib.h>
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <vector>
#include <elf.h>
#include <sys/mman.h>
#include <cstdlib>

using BbDeflate::Stream;
using BbDeflate::StatePrefix;
namespace {
bool Mapped(const void* p, std::size_t size) { return p && size <= UINTPTR_MAX - reinterpret_cast<uintptr_t>(p); }
bool Commit(void* dst, const void* src, std::size_t size) { std::memcpy(dst, src, size); return true; }
const BbDeflate::Memory memory{Mapped, Commit};
std::vector<unsigned char> Packed(const std::vector<unsigned char>& input) {
    uLongf size = compressBound(input.size());
    std::vector<unsigned char> output(size);
    assert(compress2(output.data(), &size, input.data(), input.size(), 6) == Z_OK);
    output.resize(size);
    return output;
}
void Exercise(const std::vector<unsigned char>& input) {
    auto packed = Packed(input);
    packed.insert(packed.end(), 13, 0xDA); // zlib must leave trailing data untouched
    std::vector<unsigned char> output(input.size() + 32, 0xCD), reference(input.size() + 32, 0xCD);
    StatePrefix state{}; state.wrap = 1; state.wbits = 15; state.dmax = 32768;
    Stream stream{}; stream.next_in = packed.data(); stream.avail_in = packed.size();
    stream.next_out = output.data(); stream.avail_out = output.size(); stream.state = &state;
    z_stream z{}; assert(inflateInit(&z) == Z_OK);
    z.next_in = packed.data(); z.avail_in = packed.size();
    z.next_out = reference.data(); z.avail_out = reference.size();
    assert(inflate(&z, Z_FINISH) == Z_STREAM_END);
    assert(BbDeflate::TryInflate(&stream, Z_FINISH, memory));
    assert(output == reference && stream.total_in == z.total_in && stream.total_out == z.total_out);
    assert(stream.avail_in == z.avail_in && stream.avail_out == z.avail_out);
    assert(stream.adler == z.adler && stream.data_type == z.data_type && state.mode == 26);
    assert(inflateEnd(&z) == Z_OK);
    // DONE, continuation, invalid flush, partial input and insufficient output fall back.
    const auto unchanged = [&](Stream candidate, StatePrefix candidate_state, int flush, const BbDeflate::Memory& callbacks = memory) {
        candidate.state = &candidate_state;
        const auto before = candidate;
        const auto before_state = candidate_state;
        const auto before_output = output;
        assert(!BbDeflate::TryInflate(&candidate, flush, callbacks));
        assert(!std::memcmp(&before, &candidate, sizeof(candidate)));
        assert(!std::memcmp(&before_state, &candidate_state, sizeof(candidate_state)));
        assert(output == before_output);
    };
    unchanged(stream, state, Z_FINISH);
    StatePrefix initial{}; initial.wrap = 1; initial.wbits = 15;
    Stream fresh{}; fresh.next_in = packed.data(); fresh.avail_in = packed.size() - 13;
    fresh.next_out = output.data(); fresh.avail_out = output.size();
    unchanged(fresh, initial, Z_BLOCK);
    fresh.avail_in -= 1; unchanged(fresh, initial, Z_FINISH); fresh.avail_in += 1;
    if (!input.empty()) {
        fresh.avail_out = input.size() - 1;
        unchanged(fresh, initial, Z_FINISH);
        fresh.avail_out = output.size();
    }
    auto invalid = fresh;
    invalid.next_out = packed.data();
    unchanged(invalid, initial, Z_FINISH); // overlapping compressed and output ranges
    invalid = fresh; invalid.avail_out = (8u << 20) + 1;
    unchanged(invalid, initial, Z_FINISH); // cap rejected before allocation
    invalid = fresh; invalid.next_in = nullptr;
    unchanged(invalid, initial, Z_FINISH);
    auto dictionary = initial; dictionary.havedict = 1;
    unchanged(fresh, dictionary, Z_FINISH);
    auto raw = initial; raw.wrap = 0;
    unchanged(fresh, raw, Z_FINISH);
    const BbDeflate::Memory unmapped{[](const void*, std::size_t) { return false; }, Commit};
    unchanged(fresh, initial, Z_FINISH, unmapped);
    const BbDeflate::Memory refused{Mapped, [](void*, const void*, std::size_t) { return false; }};
    unchanged(fresh, initial, Z_FINISH, refused); // failed commit preserves guest state
    packed[packed.size() - 14] ^= 0x40;
    unchanged(fresh, initial, Z_FINISH);
}
void* Alloc(void*, unsigned items, unsigned size) { return std::calloc(items, size); }
void Free(void*, void* p) { std::free(p); }
// Optional real, unmodified 1.09 inflate comparison. No proprietary bytes enter Git.
void Guest(const char* path, const char* assets) {
    std::ifstream file(path, std::ios::binary);
    Elf64_Ehdr eh{}; file.read(reinterpret_cast<char*>(&eh), sizeof(eh));
    assert(file && !std::memcmp(eh.e_ident, ELFMAG, SELFMAG) && eh.e_phentsize == sizeof(Elf64_Phdr));
    std::vector<Elf64_Phdr> ph(eh.e_phnum);
    file.seekg(eh.e_phoff); file.read(reinterpret_cast<char*>(ph.data()), ph.size() * sizeof(ph[0]));
    std::size_t extent = 0;
    for (const auto& p : ph) if (p.p_type == PT_LOAD) extent = std::max<std::size_t>(extent, p.p_vaddr + p.p_memsz);
    assert(extent >= 0x53e46f8 && extent < (128u << 20));
    auto* image = static_cast<unsigned char*>(mmap(nullptr, extent, PROT_READ | PROT_WRITE,
                                                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    assert(image != MAP_FAILED);
    for (const auto& p : ph) if (p.p_type == PT_LOAD) {
        file.seekg(p.p_offset); file.read(reinterpret_cast<char*>(image + p.p_vaddr), p.p_filesz); assert(file);
    }
    constexpr unsigned char signature[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56};
    assert(!std::memcmp(image + 0x2067ac0, signature, sizeof(signature)));
    std::uint64_t canary = 0x123456789abcdef0;
    *reinterpret_cast<void**>(image + 0x53e3be0) = &canary;
    *reinterpret_cast<void**>(image + 0x53e46f0) = reinterpret_cast<void*>(&std::memcpy);
    for (const auto& p : ph) if (p.p_type == PT_LOAD && (p.p_flags & PF_X)) {
        assert(mprotect(image + p.p_vaddr, (p.p_memsz + 4095) & ~std::size_t(4095), PROT_READ | PROT_EXEC) == 0);
    }
    auto init = reinterpret_cast<int (*)(Stream*, int, const char*, int)>(image + 0x2067900);
    auto inflate_guest = reinterpret_cast<int (*)(Stream*, int)>(image + 0x2067ac0);
    auto end = reinterpret_cast<int (*)(Stream*)>(image + 0x20697c0);
    double guest_ms = 0, native_ms = 0;
    std::uint64_t bytes = 0;
    unsigned cases = 0;
    const auto compare = [&](const std::vector<unsigned char>& packed, std::size_t size) {
        std::vector<unsigned char> slow(size + 17), fast(slow.size());
        Stream a{}, b{}; a.alloc = b.alloc = Alloc; a.free = b.free = Free;
        assert(init(&a, 15, "1.2.3", 112) == Z_OK && init(&b, 15, "1.2.3", 112) == Z_OK);
        a.next_in = b.next_in = packed.data(); a.avail_in = b.avail_in = packed.size();
        a.next_out = slow.data(); b.next_out = fast.data(); a.avail_out = b.avail_out = slow.size();
        const auto t0 = std::chrono::steady_clock::now();
        assert(inflate_guest(&a, Z_FINISH) == Z_STREAM_END);
        const auto t1 = std::chrono::steady_clock::now();
        assert(BbDeflate::TryInflate(&b, Z_FINISH, memory));
        const auto t2 = std::chrono::steady_clock::now();
        guest_ms += std::chrono::duration<double, std::milli>(t1 - t0).count();
        native_ms += std::chrono::duration<double, std::milli>(t2 - t1).count();
        assert(fast == slow && a.total_in == b.total_in && a.total_out == b.total_out);
        assert(a.total_out == size && a.adler == b.adler && a.data_type == b.data_type && a.avail_out == b.avail_out);
        assert(inflate_guest(&b, Z_NO_FLUSH) == Z_STREAM_END);
        assert(end(&a) == Z_OK && end(&b) == Z_OK);
        bytes += size; ++cases;
    };
    std::vector<unsigned char> input(256 << 10);
    for (std::size_t i = 0; i < input.size(); ++i) input[i] = (i * 17 + (i >> 6)) & 255;
    compare(Packed(input), input.size());
    if (assets) {
        std::vector<std::filesystem::path> files;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(assets))
            if (entry.is_regular_file() && entry.path().extension() == ".dcx") files.push_back(entry.path());
        std::sort(files.begin(), files.end());
        for (const auto& asset : files) {
            std::ifstream source(asset, std::ios::binary);
            unsigned char header[76]{};
            source.read(reinterpret_cast<char*>(header), sizeof(header));
            if (!source || std::memcmp(header, "DCX", 4) || std::memcmp(header + 40, "DFLT", 4)) continue;
            const auto be = [&](int off) { return (unsigned(header[off]) << 24) | (unsigned(header[off+1]) << 16) |
                                                    (unsigned(header[off+2]) << 8) | header[off+3]; };
            const auto size = be(28), length = be(32);
            if (size > (8u << 20) - 17 || length > (8u << 20) || !length) continue;
            std::vector<unsigned char> packed(length);
            source.read(reinterpret_cast<char*>(packed.data()), length); assert(source);
            compare(packed, size);
            if (cases == 257) break;
        }
        assert(cases > 1);
    }
    std::printf("Guest deflate benchmark: %u streams, %llu bytes; guest %.3f ms (%.1f MB/s), native %.3f ms (%.1f MB/s)\n",
                cases, (unsigned long long)bytes, guest_ms, bytes / guest_ms / 1000,
                native_ms, bytes / native_ms / 1000);
    munmap(image, extent);
    std::puts("PASS: unmodified guest 1.09 inflate versus native output, state and continuation");
}
}
int main(int argc, char** argv) {
    Exercise({}); Exercise({1}); Exercise(std::vector<unsigned char>(2 << 20, 7));
    std::vector<unsigned char> data(256 << 10);
    std::uint32_t rng = 0x12345678;
    for (auto& byte : data) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; byte = rng; }
    Exercise(data);
    if (argc == 2 || argc == 3) Guest(argv[1], argc == 3 ? argv[2] : nullptr);
    else assert(argc == 1);
    std::puts("PASS: native guest deflate, zlib parity and transactional fallback");
}
