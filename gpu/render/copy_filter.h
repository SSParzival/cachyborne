// SPDX-FileCopyrightText: Copyright 2026 bbport contributors
// SPDX-License-Identifier: GPL-2.0-or-later
// Ordered copy checks owned by the rasterizer; no process-wide detached worker.
#pragma once
#include "common/types.h"
#include "render/shader_hle.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <map>
#include <thread>
#include <unordered_map>
#include <vector>

namespace Vulkan {
using Ranges = std::vector<std::pair<unsigned long long, unsigned long long>>;

class CopyFilterThread {
  public:
    struct Job {
        u64 packet = 0;
        u64 epoch = 0;
        std::vector<CopyEntry> entries;
        std::vector<u8> control; ///< the kept copies' control list entries; empty: all kept
        u32 kept = 0;
        // What DispatchRecord binds by, of all its copies (as
        // CopyShaderDestinations and CopyShaderRanges give them): worked out here,
        // not on the draw recording thread.
        u64 base[2] = {};
        Ranges writes, ranges[2]; ///< destinations; sources, destinations by 64 KiB
    };

    CopyFilterThread();
    ~CopyFilterThread();
    CopyFilterThread(const CopyFilterThread&) = delete;
    CopyFilterThread& operator=(const CopyFilterThread&) = delete;

    // Producers/consumers must be quiescent. Finishes the check already in
    // flight, drops queued work and joins before any jobs or history can be
    // destroyed.
    void Stop();

    /// Stage A: the copies of the copy shader call in packet `packet` (taken from
    /// `entries`). A full queue posts nothing (stage B then finds no job and runs
    /// the call whole).
    void Post(u64 packet, std::vector<CopyEntry>& entries, u64 src_base, u64 dst_base) {
        const u64 at = head.load(std::memory_order_relaxed);
        // A slot is free once stage B took it and this thread checked it (either
        // order).
        const u64 free_from = std::min(tail.load(std::memory_order_acquire),
                                       processed.load(std::memory_order_acquire));
        if (at - free_from >= jobs.size()) {
            return;
        }
        auto& job = jobs[at % jobs.size()];
        job.packet = packet;
        job.base[0] = src_base;
        job.base[1] = dst_base;
        job.entries.swap(entries);
        head.store(at + 1, std::memory_order_release);
        wake.fetch_add(1, std::memory_order_release);
        wake.notify_one();
    }

    /// Stage B: the checked job for `packet`, or null (none: the history is
    /// dropped). Jobs of packets before it never reached here (another path ran
    /// them): dropped the same way. Not checked yet: null with `unchecked` set,
    /// taken (the call runs whole, which writes what the check will assume
    /// anyway: the history holds). Never waits for this thread.
    Job* Take(u64 packet, bool& unchecked) {
        unchecked = false;
        for (;;) {
            const u64 at = tail.load(std::memory_order_relaxed);
            if (at == head.load(std::memory_order_acquire)) {
                Invalidate();
                return nullptr;
            }
            Job& job = jobs[at % jobs.size()];
            if (job.packet == packet) {
                if (processed.load(std::memory_order_acquire) <= at) {
                    unchecked = true;
                    tail.store(at + 1, std::memory_order_release);
                    ++unchecked_calls;
                    return nullptr;
                }
                return &job;
            }
            if (job.packet > packet) {
                Invalidate();
                return nullptr;
            }
            Invalidate();
            tail.store(at + 1, std::memory_order_release);
        }
    }

    /// Stage B, after Take returned a job: done with it.
    void Release() {
        tail.store(tail.load(std::memory_order_relaxed) + 1, std::memory_order_release);
    }

    /// Stage B: a call did not run as its job said (not recorded, run whole after
    /// a check against an older history): the history starts again.
    void Invalidate() { epoch.fetch_add(1, std::memory_order_acq_rel); }
    [[nodiscard]] u64 Epoch() const { return epoch.load(std::memory_order_acquire); }

  private:
    friend struct CopyFilterTestAccess;
    struct Last {
        u64 size, hash;
    };

    /// The copies' destination (or source) ranges merged across gaps up to `gap`,
    /// as (address, size): what BuildRanges (shader_hle.cpp) makes of the same
    /// control list.
    static void MergedRanges(const std::vector<CopyEntry>& entries, bool destinations, u64 gap,
                             Ranges& out);

    void Run(std::stop_token stop);

    void Check(Job& job);

    std::array<Job, 512> jobs;
    std::atomic<u64> head{0};      ///< stage A posts
    std::atomic<u64> processed{0}; ///< this thread checked
    std::atomic<u64> tail{0};      ///< stage B took
    std::atomic<u64> epoch{0};
    std::atomic<u64> unchecked_calls{0};
    // This thread's.
    std::unordered_map<u64, Last> last; ///< destination -> its last copy
    std::map<u64, u64> ranges;          ///< destination -> size (the overlap check)
    std::vector<u64> hashes;
    std::vector<u32> keep;
    u64 history_epoch = 0, total = 0, left_out = 0, calls = 0, printed_epoch = 0;
    u32 printed = 0;
    std::atomic<u64> wake{0}; // publications and shutdown both change the wait value
    std::jthread thread;      // last: every member exists before Run can access it
};

} // namespace Vulkan
