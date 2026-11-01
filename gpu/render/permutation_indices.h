// SPDX-License-Identifier: GPL-2.0-or-later
// Adapted from bloodborne-native/bloodborne-runtime 8bc0ce68 (Pablo Souza).
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <unordered_map>

namespace Vulkan {

// Disk metadata reserves indices even for permutations not preloaded this session.
// The GPU thread owns allocation; speculative workers only read program modules.
class PermutationIndices {
public:
    static constexpr std::size_t Limit = 65536;

    bool Observe(std::uint64_t program, std::size_t index) {
        if (index >= Limit) return false;
        auto& next = next_index[program];
        next = std::max(next, index + 1);
        return true;
    }

    std::size_t Next(std::uint64_t program, std::size_t in_memory) {
        const auto found = next_index.find(program);
        const auto index = found == next_index.end() ? in_memory : std::max(in_memory, found->second);
        if (!Observe(program, index)) throw std::length_error("Shader permutation index exhausted");
        return index;
    }

    void Clear() { next_index.clear(); }

private:
    std::unordered_map<std::uint64_t, std::size_t> next_index;
};

} // namespace Vulkan
