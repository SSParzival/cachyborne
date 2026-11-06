// SPDX-License-Identifier: GPL-3.0-or-later
// BBHOST bcdd8ff: do not let a CPU poll take the previous query's answer.
#include "games/bloodborne/occlusion_fresh.h"
#include "shim/bbport_guest_code_hooks.h"
#include <array>
#include <cstring>

namespace BbOcclusionFresh {
namespace {
constexpr std::uint64_t Entry=0x1077660, Publish=0x10777d0;
constexpr std::array<unsigned char,16> EntryBytes{
    0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x53,0x50,0x49,0x89,0xd6,0x41,0x89,0xf7};
constexpr std::array<unsigned char,6> Refused{0x0f,0x84,0x30,0x01,0x00,0x00};
constexpr std::array<unsigned char,5> Kind{0x45,0x85,0xff,0x74,0x09};
constexpr std::array<unsigned char,5> PublishBytes{0x48,0x83,0x43,0x10,0x10};
void __attribute__((sysv_abi)) Accepted(std::uint64_t,const Core::GuestRegisterSnapshot* regs) {
    if (std::uint32_t(regs->r15)!=0) return;
    // This site is reached only after accepting and writing the query packets,
    // before the final command-buffer write-pointer update. Refused begins
    // branch directly to the epilogue, skipping this observer. End/other kinds
    // keep their block. No renderer-thread clear can close this CPU-side race.
    auto* block=reinterpret_cast<void*>(regs->r14&~std::uint64_t(7));
    std::memset(block,0,128);
}
}
bool Install(unsigned char* image,std::uint64_t size) {
    if (!image || size<Publish+PublishBytes.size() ||
        std::memcmp(image+Entry,EntryBytes.data(),EntryBytes.size()) ||
        std::memcmp(image+0x107769f,Refused.data(),Refused.size()) ||
        std::memcmp(image+0x10776a5,Kind.data(),Kind.size()) ||
        std::memcmp(image+Publish,PublishBytes.data(),PublishBytes.size())) return false;
    return Core::InstallGuestCodeHook(image+Publish,PublishBytes,0,Accepted);
}
}
