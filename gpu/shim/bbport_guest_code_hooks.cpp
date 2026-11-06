// SPDX-FileCopyrightText: Copyright 2026 bbport contributors
// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: observers in a game's own code (bbport_guest_code_hooks.h). The trampoline follows
// shadPS4's InstallGuestCodeHook (core/cpu_patches.cpp of the shadp2p/BBSC forks, GPL-2.0+); the
// trampoline memory and the patching are the port's: the game's code is mapped read and execute
// here, so the jump is written under a short write permission.

#include "bbport_map_near.h"
#include "bbport_guest_code_hooks.h"
#include "bbport_guest_patch.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <map>
#include <vector>
#include <sys/mman.h>
#include <unistd.h>

#include <xbyak/xbyak.h>
#include <Zydis/Zydis.h>

#include "common/decoder.h"

namespace Core {
namespace {

using namespace Xbyak::util;

constexpr std::size_t NearJumpBytes = 5;
constexpr std::size_t GuestRedZoneBytes = 128;
constexpr std::size_t AreaSize = 4u << 20; // trampolines (~400 bytes each)
constexpr std::int64_t JumpReach = 0x7000'0000; // inside a rel32 jump's 2 GiB

std::mutex hook_mutex;
std::map<std::uintptr_t, std::size_t> hooked_sites; // exact overwritten instruction spans

/// An executable area within reach of its code, mapped on first use.
struct Area {
    std::uintptr_t start = 0;
    std::unique_ptr<Xbyak::CodeGenerator> gen;
};
std::vector<Area> areas;

std::uintptr_t MapNear(std::uintptr_t code) {
    // Below the code first (the port maps the game's image at 0x800000000 with room under it),
    // then above, in 64 MiB steps; else the runtime's low region (bbport_map_near.h).
    return reinterpret_cast<std::uintptr_t>(BbMapNear::Map(
        code, code + 64, AreaSize, PROT_READ | PROT_WRITE | PROT_EXEC, 64ull << 20, "Guest code hooks"));
}

Xbyak::CodeGenerator* AreaFor(std::uintptr_t code) {
    for (auto& area : areas) {
        const std::int64_t distance = std::int64_t(area.start) - std::int64_t(code);
        if (distance > -JumpReach && distance < JumpReach &&
            area.gen->getSize() + 1024 < AreaSize) {
            return area.gen.get();
        }
    }
    const std::uintptr_t start = MapNear(code);
    if (!start) {
        return nullptr;
    }
    auto gen = std::make_unique<Xbyak::CodeGenerator>(AreaSize, reinterpret_cast<void*>(start));
    areas.push_back({start, std::move(gen)});
    return areas.back().gen.get();
}

bool WholeAndAbsolute(const std::uint8_t* code, std::size_t size) {
    std::size_t offset = 0;
    while (offset < size) {
        ZydisDecodedInstruction instruction{};
        std::array<ZydisDecodedOperand, ZYDIS_MAX_OPERAND_COUNT> operands{};
        const auto status = Common::Decoder::Instance()->decodeInstruction(
            instruction, operands.data(), const_cast<std::uint8_t*>(code) + offset, size - offset);
        if (!ZYAN_SUCCESS(status) || instruction.length == 0 ||
            instruction.length > size - offset) {
            std::fprintf(stderr, "Guest code hook at %p: not whole instructions\n",
                         static_cast<const void*>(code));
            return false;
        }
        for (std::uint8_t i = 0; i < instruction.operand_count; ++i) {
            const auto& operand = operands[i];
            if ((operand.type == ZYDIS_OPERAND_TYPE_IMMEDIATE && operand.imm.is_relative) ||
                (operand.type == ZYDIS_OPERAND_TYPE_MEMORY &&
                 (operand.mem.base == ZYDIS_REGISTER_RIP || operand.mem.base == ZYDIS_REGISTER_EIP))) {
                std::fprintf(stderr, "Guest code hook at %p: relative addressing\n",
                             static_cast<const void*>(code));
                return false;
            }
        }
        offset += instruction.length;
    }
    return true;
}

std::atomic<bool> code_writable{false};

} // namespace

int GuestCodeProtection() {
    return code_writable.load(std::memory_order_acquire) ? PROT_READ | PROT_WRITE | PROT_EXEC
                                                         : PROT_READ | PROT_EXEC;
}

void KeepGuestCodeWritable() {
    code_writable.store(true, std::memory_order_release);
}

static bool InstallHook(void* address, std::span<const std::uint8_t> expected, std::uint64_t tag,
                        std::uint64_t hook, const std::uint8_t* skip_flag, bool replace,
                        void** original, bool conditional) {
    constexpr std::size_t SnapshotOffset = 0;
    constexpr std::size_t ScratchR11Offset = sizeof(GuestRegisterSnapshot);
    constexpr std::size_t ScratchRflagsOffset = ScratchR11Offset + sizeof(std::uint64_t);
    constexpr std::size_t FrameReserve =
        (ScratchRflagsOffset + sizeof(std::uint64_t) + GuestRedZoneBytes + 15) & ~std::size_t(15);

    auto* code = static_cast<std::uint8_t*>(address);
    if (!hook || expected.size() < NearJumpBytes || (replace && skip_flag) || (original && !replace && !conditional)) {
        return false;
    }
    std::scoped_lock lock{hook_mutex};
    if (std::memcmp(code, expected.data(), expected.size()) != 0) {
        std::fprintf(stderr, "Guest code hook at %p: the code differs (another build?)\n", address);
        return false;
    }
    if (!WholeAndAbsolute(code, expected.size())) {
        return false;
    }
    const auto begin = reinterpret_cast<std::uintptr_t>(code);
    const auto next = hooked_sites.lower_bound(begin);
    const bool overlaps_next = next != hooked_sites.end() && next->first < begin + expected.size();
    const bool overlaps_previous = next != hooked_sites.begin() &&
        std::prev(next)->first + std::prev(next)->second > begin;
    if (overlaps_next || overlaps_previous) {
        std::fprintf(stderr, "Guest code hook at %p overlaps another one\n", address);
        return false;
    }
    Xbyak::CodeGenerator* t = AreaFor(begin);
    if (!t) {
        std::fprintf(stderr, "Guest code hook at %p: no trampoline memory in reach\n", address);
        return false;
    }
    const std::size_t start_offset = t->getSize();
    const auto* trampoline = t->getCurr();
    const void* original_entry = nullptr;
    const auto snapshot = [](std::size_t field) { return qword[rsp + SnapshotOffset + field]; };
    try {
        t->lea(rsp, ptr[rsp - FrameReserve]); // past the game's red zone
        t->mov(qword[rsp + ScratchR11Offset], r11);
        t->pushfq();
        t->pop(r11);
        t->mov(qword[rsp + ScratchRflagsOffset], r11);
        t->mov(r11, rsp);
        t->and_(rsp, -16);
        t->mov(snapshot(offsetof(GuestRegisterSnapshot, rax)), rax);
        t->mov(snapshot(offsetof(GuestRegisterSnapshot, rbx)), rbx);
        t->mov(snapshot(offsetof(GuestRegisterSnapshot, rcx)), rcx);
        t->mov(snapshot(offsetof(GuestRegisterSnapshot, rdx)), rdx);
        t->mov(snapshot(offsetof(GuestRegisterSnapshot, rsi)), rsi);
        t->mov(snapshot(offsetof(GuestRegisterSnapshot, rdi)), rdi);
        t->mov(snapshot(offsetof(GuestRegisterSnapshot, rbp)), rbp);
        t->mov(snapshot(offsetof(GuestRegisterSnapshot, r8)), r8);
        t->mov(snapshot(offsetof(GuestRegisterSnapshot, r9)), r9);
        t->mov(snapshot(offsetof(GuestRegisterSnapshot, r10)), r10);
        t->mov(snapshot(offsetof(GuestRegisterSnapshot, r12)), r12);
        t->mov(snapshot(offsetof(GuestRegisterSnapshot, r13)), r13);
        t->mov(snapshot(offsetof(GuestRegisterSnapshot, r14)), r14);
        t->mov(snapshot(offsetof(GuestRegisterSnapshot, r15)), r15);
        t->mov(rax, qword[r11 + ScratchR11Offset]);
        t->mov(snapshot(offsetof(GuestRegisterSnapshot, r11)), rax);
        t->mov(rax, qword[r11 + ScratchRflagsOffset]);
        t->mov(snapshot(offsetof(GuestRegisterSnapshot, rflags)), rax);
        t->lea(rax, ptr[r11 + FrameReserve]);
        t->mov(snapshot(offsetof(GuestRegisterSnapshot, rsp)), rax);
        t->stmxcsr(dword[rsp + SnapshotOffset + offsetof(GuestRegisterSnapshot, mxcsr)]);
        for (int i = 0; i < 16; ++i) {
            t->vmovdqu(yword[rsp + SnapshotOffset + offsetof(GuestRegisterSnapshot, ymm) + i * 32],
                       Xbyak::Ymm(i));
        }
        // The aligned snapshot in a callee-saved register across the call.
        t->mov(r12, rsp);
        t->mov(rdi, tag);
        t->lea(rsi, ptr[r12 + SnapshotOffset]);
        t->mov(rax, hook);
        t->call(rax);
        t->mov(rsp, r12);
        // Decide while the result is still in AL, before restoring any guest
        // register. Each invocation branches using its own callback result;
        // nested and simultaneous calls never share a skip flag.
        Xbyak::Label return_from_guest;
        if (conditional) {
            t->test(al, al);
            t->jnz(return_from_guest, Xbyak::CodeGenerator::LabelType::T_NEAR);
        }
        const auto restore = [&] {
            for (int i = 0; i < 16; ++i) {
                t->vmovdqu(Xbyak::Ymm(i),
                           yword[rsp + SnapshotOffset + offsetof(GuestRegisterSnapshot, ymm) + i * 32]);
            }
            t->ldmxcsr(dword[rsp + SnapshotOffset + offsetof(GuestRegisterSnapshot, mxcsr)]);
            t->mov(rbx, snapshot(offsetof(GuestRegisterSnapshot, rbx)));
            t->mov(rcx, snapshot(offsetof(GuestRegisterSnapshot, rcx)));
            t->mov(rdx, snapshot(offsetof(GuestRegisterSnapshot, rdx)));
            t->mov(rsi, snapshot(offsetof(GuestRegisterSnapshot, rsi)));
            t->mov(rdi, snapshot(offsetof(GuestRegisterSnapshot, rdi)));
            t->mov(rbp, snapshot(offsetof(GuestRegisterSnapshot, rbp)));
            t->mov(r8, snapshot(offsetof(GuestRegisterSnapshot, r8)));
            t->mov(r9, snapshot(offsetof(GuestRegisterSnapshot, r9)));
            t->mov(r10, snapshot(offsetof(GuestRegisterSnapshot, r10)));
            t->mov(r11, snapshot(offsetof(GuestRegisterSnapshot, r11)));
            t->mov(r12, snapshot(offsetof(GuestRegisterSnapshot, r12)));
            t->mov(r13, snapshot(offsetof(GuestRegisterSnapshot, r13)));
            t->mov(r14, snapshot(offsetof(GuestRegisterSnapshot, r14)));
            t->mov(r15, snapshot(offsetof(GuestRegisterSnapshot, r15)));
            t->mov(rax, snapshot(offsetof(GuestRegisterSnapshot, rflags)));
            t->push(rax);
            t->popfq();
            t->mov(rax, snapshot(offsetof(GuestRegisterSnapshot, rax)));
            t->mov(rsp, snapshot(offsetof(GuestRegisterSnapshot, rsp)));
        };
        restore();
        Xbyak::Label run_original;
        if (replace) t->ret();
        if (skip_flag) {
            // Function entries only: [rsp] is the return address, flags are not live across a
            // call; a set flag (cleared here) returns from the game's function at once.
            t->push(rax);
            t->mov(rax, reinterpret_cast<std::uint64_t>(skip_flag));
            t->cmp(byte[rax], 0);
            t->mov(byte[rax], 0);
            t->pop(rax);
            t->je(run_original, Xbyak::CodeGenerator::LabelType::T_NEAR);
            t->ret();
        }
        t->L(run_original);
        original_entry = t->getCurr();
        t->db(expected.data(), expected.size());
        t->jmp(reinterpret_cast<const void*>(code + expected.size()),
               Xbyak::CodeGenerator::LabelType::T_NEAR);
        if (conditional) {
            t->L(return_from_guest);
            restore();
            t->ret();
        }
    } catch (const Xbyak::Error& error) {
        t->setSize(start_offset);
        std::fprintf(stderr, "Guest code hook at %p: trampoline failed (%s)\n", address,
                     error.what());
        return false;
    }
    // The site: a jump to the trampoline, the rest of the span filled with nops.
    std::vector<std::uint8_t> patch(expected.size(), 0x90);
    const std::int64_t rel = std::int64_t(reinterpret_cast<std::uintptr_t>(trampoline)) -
                             std::int64_t(begin + NearJumpBytes);
    patch[0] = 0xe9;
    const std::int32_t rel32 = std::int32_t(rel);
    std::memcpy(patch.data() + 1, &rel32, 4);
    if (original) *original = const_cast<void*>(original_entry);
    const auto result=BbGuestPatch::Write(begin,patch,expected,GuestCodeProtection(),true);
    if (result!=BbGuestPatch::Result::Applied && result!=BbGuestPatch::Result::AlreadyApplied) {
        if (original) *original=nullptr;
        t->setSize(start_offset);
        return false;
    }
    hooked_sites.emplace(begin, expected.size());
    return true;
}

bool InstallGuestCodeHook(void* address, std::span<const std::uint8_t> expected,
                          std::uint64_t tag, GuestCodeHook hook,
                          const std::uint8_t* skip_flag, bool replace, void** original) {
    return InstallHook(address, expected, tag, reinterpret_cast<std::uint64_t>(hook),
                       skip_flag, replace, original, false);
}

bool InstallConditionalGuestCodeHook(void* address, std::span<const std::uint8_t> expected,
                                     std::uint64_t tag, ConditionalGuestCodeHook hook,
                                     void** original) {
    return InstallHook(address, expected, tag, reinterpret_cast<std::uint64_t>(hook),
                       nullptr, false, original, true);
}

} // namespace Core
