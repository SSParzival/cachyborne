// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace BbGuestCall {
// SysV AMD64 scalar call frame, matching BBHOST's GuestCallRegs layout.
// The caller owns a valid target, attached guest thread and game context.
// This bridge does not attach threads, switch TLS or marshal aggregate/vector
// arguments. XMM entries contain the low 64 bits (float uses the low 32 bits).
struct Registers {
    void* fn{};
    std::uint64_t ints[6]{};
    std::uint64_t xmm[8]{};
    std::uint64_t stack[4]{};
    std::uint64_t ret{};
    std::uint64_t ret_xmm0{};
};
static_assert(std::is_standard_layout_v<Registers>);
static_assert(sizeof(Registers) == 168);
static_assert(offsetof(Registers, fn) == 0);
static_assert(offsetof(Registers, ints) == 8);
static_assert(offsetof(Registers, xmm) == 56);
static_assert(offsetof(Registers, stack) == 120);
static_assert(offsetof(Registers, ret) == 152);
static_assert(offsetof(Registers, ret_xmm0) == 160);
// Null targets are refused without changing the frame. Other target/context
// validation belongs to the owner of the call. Calls may nest or run in parallel.
bool Invoke(Registers& registers);
}
