// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "bbport_guest_patch.h"
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace BbPluginImage {
using Mapped = bool (*)(void*,std::uintptr_t,std::size_t);
// The host supplies the original, unmodified ELF and its relocated native
// image, before starting guest threads. Identity is computed from the file,
// never inferred from patched runtime bytes or an environment flag.
class Image {
public:
    static std::unique_ptr<Image> Create(std::span<const std::uint8_t> original_elf,
        unsigned char* image,std::size_t image_size,Mapped mapped=nullptr,void* context=nullptr);
    const std::string& Sha256() const { return file_sha; }
    const std::string& MappedSha256() const { return mapped_sha; }
    bool Is109() const;
    std::uintptr_t GuestAddress(std::uint64_t bn,std::size_t bytes=1) const;
    bool Read(std::uint64_t address,void* out,std::size_t bytes) const;
    // BBHOST write: original 1.09 image and BN addresses only, never heap or
    // host addresses. Live writes stay within ELF writable/non-executable data
    // segments and currently writable/non-executable pages. Quiescent image
    // phases may change other image bytes, restoring original page permissions.
    // The caller serializes guest data access and keeps the image mapped.
    BbGuestPatch::Result Write(std::uint64_t bn,std::span<const std::uint8_t> bytes,
                               bool quiescent=false) const;
    // Image-phase/quiescent use only. Sources and destination are checked by
    // the shared native patch writer; existing page permissions are restored.
    BbGuestPatch::Result Patch(std::uint64_t bn,std::span<const std::uint8_t> expected,
        std::span<const std::uint8_t> bytes) const;
private:
    std::uintptr_t Address(std::uint64_t bn,std::size_t bytes) const;
    std::uintptr_t base=0;
    std::size_t extent=0;
    std::string file_sha,mapped_sha;
    std::vector<std::pair<std::uint64_t,std::uint64_t>> writable;
    Mapped mapped=nullptr;
    void* context=nullptr;
};
}
