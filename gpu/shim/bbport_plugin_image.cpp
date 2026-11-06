// SPDX-License-Identifier: GPL-3.0-or-later
#include "bbport_plugin_image.h"
#include "common/guest_memory.h"
#include "common/sha256.h"
#include <algorithm>
#include <cstring>
#include <elf.h>
#include <limits>
#include <vector>

namespace BbPluginImage {
namespace {
constexpr std::uint64_t Preferred=0x400000, MaxImage=512u<<20;
// The mapped 1.09 image identity used by scripts/game_check.py, independent
// of ELF/SELF headers and dump-tool padding. Never spoof BBHOST's file hash.
constexpr const char* Supported="071df19c8880086d97182dbc057bc8cb37badaca57d9112683836b24a0444c0a";
}
std::unique_ptr<Image> Image::Create(std::span<const std::uint8_t> elf,
    unsigned char* image,std::size_t image_size,Mapped mapped,void* context) {
    const auto base=reinterpret_cast<std::uintptr_t>(image);
    if (!base || image_size>MaxImage || !BbGuestMemory::Range(base,image_size) ||
        elf.size()>MaxImage || elf.size()<sizeof(Elf64_Ehdr)) return {};
    Elf64_Ehdr header;std::memcpy(&header,elf.data(),sizeof(header));
    constexpr unsigned char magic[]={0x7f,'E','L','F',ELFCLASS64,ELFDATA2LSB,EV_CURRENT};
    if (std::memcmp(header.e_ident,magic,sizeof(magic)) || header.e_machine!=EM_X86_64 ||
        header.e_phentsize!=sizeof(Elf64_Phdr) || !header.e_phnum || header.e_phnum>256 ||
        header.e_phoff>elf.size() || header.e_phnum>(elf.size()-header.e_phoff)/sizeof(Elf64_Phdr)) return {};
    std::vector<Elf64_Phdr> loads;
    std::size_t extent=0;
    for (unsigned i=0;i<header.e_phnum;++i) {
        Elf64_Phdr p;std::memcpy(&p,elf.data()+header.e_phoff+i*sizeof(p),sizeof(p));
        if (p.p_type!=PT_LOAD && p.p_type!=0x61000010) continue;
        if (p.p_vaddr>image_size || p.p_memsz>image_size-p.p_vaddr || p.p_filesz>p.p_memsz ||
            p.p_offset>elf.size() || p.p_filesz>elf.size()-p.p_offset) return {};
        extent=std::max<std::size_t>(extent,p.p_vaddr+p.p_memsz);loads.push_back(p);
    }
    if (!extent) return {};
    std::vector<std::uint8_t> canonical(extent);
    for (const auto& p:loads) std::memcpy(canonical.data()+p.p_vaddr,elf.data()+p.p_offset,p.p_filesz);
    auto result=std::unique_ptr<Image>(new Image);
    result->base=base;result->extent=extent;result->mapped=mapped;result->context=context;
    result->file_sha=sha256_hex(elf.data(),elf.size());
    result->mapped_sha=sha256_hex(canonical.data(),canonical.size());
    for (const auto& p:loads)
        if ((p.p_flags&PF_W) && !(p.p_flags&PF_X) && p.p_memsz)
            result->writable.emplace_back(p.p_vaddr,p.p_vaddr+p.p_memsz);
    std::ranges::sort(result->writable);
    // Adjacent/overlapping writable segments can cover a single data write.
    std::size_t kept=0;
    for (const auto range:result->writable) {
        if (kept && range.first<=result->writable[kept-1].second)
            result->writable[kept-1].second=std::max(result->writable[kept-1].second,range.second);
        else result->writable[kept++]=range;
    }
    result->writable.resize(kept);
    return result;
}
bool Image::Is109() const { return mapped_sha==Supported; }
std::uintptr_t Image::Address(std::uint64_t bn,std::size_t bytes) const {
    if (bn<Preferred || bn-Preferred>extent || bytes>extent-(bn-Preferred)) return 0;
    return base+std::uintptr_t(bn-Preferred);
}
std::uintptr_t Image::GuestAddress(std::uint64_t bn,std::size_t bytes) const { return Is109()?Address(bn,bytes):0; }
bool Image::Read(std::uint64_t address,void* out,std::size_t bytes) const {
    auto at=Address(address,bytes);
    if (!at) {
        if (address<base || address-base>extent || bytes>extent-(address-base)) {
            if (!BbGuestMemory::Range(address,bytes) || !mapped || !mapped(context,address,bytes)) return false;
        }
        at=address;
    }
    return BbGuestMemory::Read(nullptr,at,out,bytes);
}
BbGuestPatch::Result Image::Write(std::uint64_t bn,std::span<const std::uint8_t> bytes,
                                 bool quiescent) const {
    const auto at=Is109()?Address(bn,bytes.size()):0;
    if (!at || bytes.empty()) return BbGuestPatch::Result::Refused;
    if (!quiescent) {
        const auto offset=bn-Preferred;
        if (std::ranges::none_of(writable,[&](const auto& range) {
            return offset>=range.first && offset<range.second && bytes.size()<=range.second-offset;
        })) return BbGuestPatch::Result::Refused;
    }
    return BbGuestPatch::Write(at,bytes,{},-1,false,
        quiescent?BbGuestPatch::Mode::Quiescent:BbGuestPatch::Mode::WritableData);
}
BbGuestPatch::Result Image::Patch(std::uint64_t bn,std::span<const std::uint8_t> expected,
    std::span<const std::uint8_t> bytes) const {
    const auto at=Is109()?Address(bn,bytes.size()):0;
    if (!at || expected.size()!=bytes.size()) return BbGuestPatch::Result::Refused;
    return BbGuestPatch::Write(at,bytes,expected);
}
}
