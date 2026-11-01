// SPDX-License-Identifier: GPL-2.0-or-later
#include "hooks_internal.h"
#include "bbport_guest_guards.h"

namespace BbGuestHooks {
bool InstallOnlineEventGuard() {
    constexpr u64 site = 0x1eb3de8;
    constexpr u64 unknown = 0x1eb3dd0;
    const auto& expected = BbGuestCode::OnlineEventBytes;
    auto* code = reinterpret_cast<u8*>(ImageBase + site);
    if (std::memcmp(code, expected.data(), expected.size()) != 0)
        return false;
    const auto e =
        BbGuestCode::OnlineEventGuard(ImageBase + site + expected.size(), ImageBase + unknown);
    const u64 stub = PlaceStub(e);
    u32 rel;
    if (!stub || !Rel32To(ImageBase + site + 5, stub, rel))
        return false;
    // Install runs before guest threads start. Use the allocated stub page, with no shared
    // code cave or dependency on Quit to Desktop's patch layout (community eec77ba2).
    const auto page_size = u64(sysconf(_SC_PAGESIZE));
    void* page = reinterpret_cast<void*>((ImageBase + site) & ~(page_size - 1));
    const u64 length = ImageBase + site + expected.size() - reinterpret_cast<u64>(page);
    if (mprotect(page, length, PROT_READ | PROT_WRITE | PROT_EXEC) != 0)
        return false;
    code[0] = 0xe9;
    std::memcpy(code + 1, &rel, sizeof(rel));
    std::fill(code + 5, code + expected.size(), 0x90);
    mprotect(page, length, Core::GuestCodeProtection());
    return true;
}
} // namespace BbGuestHooks
