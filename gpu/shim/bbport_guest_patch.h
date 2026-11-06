// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <span>

namespace BbGuestPatch {
enum class Result { Applied, AlreadyApplied, Mismatch, Refused };
enum class Mode { Quiescent, WritableData };
// Quiescent code destination required: this is not atomic live code patching.
// Reads sources safely, checks the entire destination, temporarily adds write
// access and restores each mapping's permissions. -1 preserves actual page
// protections; an explicit final protection supports the native hook policy.
// Permission restoration or rollback failure is fatal: never resume guest code
// with partially changed instructions or unintentionally writable code pages.
// WritableData never changes permissions and refuses executable or nonwritable
// mappings before writing. The caller still serializes guest data access; this
// mutex does not exclude raw guest stores or allocation retirement.
Result Write(std::uintptr_t destination,std::span<const std::uint8_t> bytes,
    std::span<const std::uint8_t> expected={},int final_protection=-1,
    bool opcode_last=false,Mode mode=Mode::Quiescent);
}
