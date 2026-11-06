// SPDX-License-Identifier: GPL-2.0-or-later
// Retail addresses shared with Cachyborne's existing BBSC kit restoration.
#include "event_flags.h"
#include "common/guest_memory.h"
#include <cstring>
namespace BbEventFlags {
namespace {
constexpr std::uintptr_t Manager=0x553b100,GetFlag=0x1399bf0,SetFlag=0x132e5a0;
bool Ready(std::uintptr_t image) {
    std::uintptr_t manager=0;
    return image>=0x10000 && image<=UINTPTR_MAX-Manager && BbGuestMemory::Read(image+Manager,manager) && manager>=0x10000;
}
}
bool Validate(unsigned char* image,std::uint64_t size) {
    constexpr unsigned char get[]{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x53,0x50,0x41,0x89,0xf7};
    constexpr unsigned char set[]{0x55,0x48,0x89,0xe5,0x41,0x56,0x53,0x41,0x89,0xf6};
    return image && size>=Manager+8 && !std::memcmp(image+GetFlag,get,sizeof get) &&
        !std::memcmp(image+SetFlag,set,sizeof set);
}
bool Read(std::uintptr_t image,std::span<const std::uint32_t> ids,std::span<bool> values) {
    if (values.size()!=ids.size() || !Ready(image)) return false;
    const auto fn=reinterpret_cast<bool(__attribute__((sysv_abi)) *)(std::uint64_t,std::uint32_t)>(image+GetFlag);
    for (std::size_t i=0;i<ids.size();++i) values[i]=fn(0,ids[i]);
    return true;
}
bool Set(std::uintptr_t image,std::uint32_t id,bool value) {
    if (!Ready(image)) return false;
    reinterpret_cast<std::uint64_t(__attribute__((sysv_abi)) *)(std::uint64_t,std::uint32_t,std::uint8_t)>(image+SetFlag)(0,id,value);
    return true;
}
}
