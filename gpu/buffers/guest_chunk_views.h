// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "buffers/buffer.h"
#include "shim/bbport_guest_memory.h"
#include <fmt/format.h>
#include <memory>
#include <unordered_map>

namespace VideoCore {

// Chunk indices count allocations over the whole session, not simultaneous
// live chunks. Key by the full index: modulo MaxChunks aliases unrelated
// allocations after enough heap growth/shrink cycles.
class GuestChunkViews {
public:
    Buffer& Get(const Vulkan::Instance& instance, const BbGuestMemory::Chunk& chunk) {
        auto& view = views[chunk.index];
        if (!view) {
            view = std::make_unique<Buffer>(instance, chunk.size, chunk.memory,
                                           fmt::format("bbport guest memory {:#x}", chunk.phys));
        }
        return *view;
    }

    // Transfer ownership to the scheduler's retirement callback, removing only
    // this exact chunk from future bindings. No Vulkan object is destroyed here.
    std::unique_ptr<Buffer> Take(u32 index) {
        auto entry = views.extract(index);
        return entry.empty() ? nullptr : std::move(entry.mapped());
    }

    bool Contains(const Buffer* buffer) const {
        for (const auto& [index, view] : views) {
            if (view.get() == buffer) return true;
        }
        return false;
    }

private:
    std::unordered_map<u32, std::unique_ptr<Buffer>> views;
};

} // namespace VideoCore
