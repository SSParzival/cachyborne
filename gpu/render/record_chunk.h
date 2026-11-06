// SPDX-FileCopyrightText: Copyright 2026 bbport contributors
// SPDX-License-Identifier: GPL-2.0-or-later
// bbport render: blocks of deferred Vulkan commands for the recording threads (RecordChunk),
// and the command buffer segments of one submission (SubmissionBatch).
#pragma once

#include <array>
#include <cstddef>
#include <limits>
#include <memory>
#include <new>
#include <utility>
#include <vector>

#include "bbport_toggles.h"
#include "common/types.h"
#include "vulkan/vk_common.h"

namespace Vulkan {

/// The command buffer segments of one submission and their chunk counters (guarded by the
/// scheduler's recorder_mutex). With BB_ASYNC_SUBMIT several are in flight: a submission goes
/// out from a recording thread once its segments are recorded.
struct SubmissionBatch {
    static constexpr u32 MaxSegments = 64;
    std::array<vk::CommandBuffer, MaxSegments> segments{}; ///< null until begun
    std::array<u32, MaxSegments> handed{}, recorded{};
    void Reset() {
        segments.fill({});
        handed.fill(0);
        recorded.fill(0);
    }
};

/// A block of deferred Vulkan commands. Commands are closures placed in
/// fixed storage (no allocation per command) and run in order on the recording thread.
class RecordChunk {
public:
    static constexpr size_t Capacity = 128 * 1024;

    RecordChunk() = default;
    RecordChunk(const RecordChunk&) = delete;
    RecordChunk& operator=(const RecordChunk&) = delete;
    ~RecordChunk() {
        // Cancellation must release captured resources without running Vulkan.
        for (auto* command = first; command;) {
            auto* next = command->next;
            command->~CommandBase();
            command = next;
        }
    }

    /// Overflow stays owned by this chunk: never separate a command from the
    /// RecordData spans it reads. Allocation failures propagate before recording.
    template <typename Func>
    bool Push(Func&& func) {
        using Command = TypedCommand<std::decay_t<Func>>;
        static_assert(sizeof(Command) <= Capacity, "recorded command is too large");
        auto* command = new (Allocate(sizeof(Command), alignof(Command)))
            Command(std::forward<Func>(func));
        if (last) {
            last->next = command;
        } else {
            first = command;
        }
        last = command;
        return true;
    }

    /// Raw storage lives through Execute, including overflow and over-aligned data.
    void* Allocate(size_t bytes, size_t align) {
        if (!align || (align & (align - 1)) || bytes > std::numeric_limits<size_t>::max() - align) {
            throw std::bad_array_new_length();
        }
        const size_t offset = (used + align - 1) & ~(align - 1);
        if (align <= 64 && offset <= Capacity && bytes <= Capacity - offset) {
            used = offset + bytes;
            PrefetchAhead();
            return storage + offset;
        }
        const auto alignment = std::align_val_t(align < 64 ? 64 : align);
        std::unique_ptr<std::byte, AlignedDelete> block{
            static_cast<std::byte*>(::operator new(bytes, alignment)), AlignedDelete{alignment}};
        auto* result = block.get();
        spills.push_back(std::move(block));
        // Size is a batching hint; saturate rather than wrapping on huge input.
        spill_bytes = bytes > std::numeric_limits<size_t>::max() - Capacity - spill_bytes
            ? std::numeric_limits<size_t>::max() - Capacity : spill_bytes + bytes;
        return result;
    }

    void Execute(vk::CommandBuffer cmdbuf) {
        for (CommandBase* command = first; command;) {
            CommandBase* const next = command->next;
            command->Execute(cmdbuf);
            command->~CommandBase();
            command = next;
        }
        first = last = nullptr;
        used = 0;
        spills.clear();
        spill_bytes = 0;
    }

    [[nodiscard]] bool Empty() const noexcept {
        return first == nullptr;
    }

    [[nodiscard]] size_t Size() const noexcept {
        return used + spill_bytes;
    }

    u32 segment = 0; ///< the command buffer segment the chunk is recorded into (Scheduler)
    SubmissionBatch* batch = nullptr; ///< the submission it belongs to (Scheduler)
    /// Ordered chunks: the command chunks of `segment` (and all of the segments before it)
    /// that must be recorded before this chunk runs.
    u32 after = 0;

private:
    /// The recording thread last read these cache lines: the first write to each has to take
    /// it back from that core. Requesting ownership a few lines ahead overlaps those
    /// transfers instead of stalling on each (RecordPrefetch).
    void PrefetchAhead() const {
        if (!BbToggle::Disabled(BbToggle::RecordPrefetch)) {
            if (used + 384 < Capacity) __builtin_prefetch(storage + used + 384, 1, 3);
            if (used + 448 < Capacity) __builtin_prefetch(storage + used + 448, 1, 3);
        }
    }

    struct CommandBase {
        virtual ~CommandBase() = default;
        virtual void Execute(vk::CommandBuffer cmdbuf) = 0;
        CommandBase* next{};
    };
    template <typename Func>
    struct TypedCommand final : CommandBase {
        explicit TypedCommand(Func&& func_) : func{std::move(func_)} {}
        explicit TypedCommand(const Func& func_) : func{func_} {}
        void Execute(vk::CommandBuffer cmdbuf) override {
            func(cmdbuf);
        }
        Func func;
    };

    alignas(64) std::byte storage[Capacity];
    size_t used = 0;
    CommandBase* first{};
    CommandBase* last{};
    struct AlignedDelete {
        std::align_val_t alignment;
        void operator()(std::byte* p) const noexcept { ::operator delete(p, alignment); }
    };
    std::vector<std::unique_ptr<std::byte, AlignedDelete>> spills;
    size_t spill_bytes = 0;
};

} // namespace Vulkan
