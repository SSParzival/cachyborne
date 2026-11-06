// SPDX-FileCopyrightText: Copyright 2026 bbport contributors
// SPDX-License-Identifier: GPL-2.0-or-later
// The copy history hashes ~5000 small copies a frame: inline xxHash, not a DLL call each.
#define XXH_INLINE_ALL
#include "render/copy_filter.h"
#include "common/thread.h"
#include "shim/bb_env.h"
#include "shim/bbport_toggles.h"
#include <cstdio>
#include <cstring>
#include <xxhash.h>

namespace Vulkan {
CopyFilterThread::CopyFilterThread() : thread{[this](std::stop_token stop) { Run(stop); }} {}
CopyFilterThread::~CopyFilterThread() { Stop(); }

void CopyFilterThread::Stop() {
    if (!thread.joinable()) {
        return;
    }
    thread.request_stop();
    // notify alone cannot release atomic::wait if the value has not changed.
    // Keep this separate from head: shutdown must never publish a fictitious job.
    wake.fetch_add(1, std::memory_order_release);
    wake.notify_one();
    thread.join();
}

void CopyFilterThread::Run(std::stop_token stop) {
    Common::SetCurrentThreadName("bb:CopyFilter");
    for (u64 at = 0; !stop.stop_requested(); ++at) {
        for (;;) {
            const u64 observed = wake.load(std::memory_order_acquire);
            if (stop.stop_requested()) {
                return;
            }
            if (head.load(std::memory_order_acquire) > at) {
                break;
            }
            wake.wait(observed, std::memory_order_acquire);
        }
        if (stop.stop_requested()) {
            return;
        }
        Check(jobs[at % jobs.size()]);
        processed.store(at + 1, std::memory_order_release);
    }
}

void CopyFilterThread::MergedRanges(const std::vector<CopyEntry>& entries, bool destinations,
                                    u64 gap, Ranges& out) {
    out.clear();
    for (const auto& e : entries) {
        const u64 begin = destinations ? e.dst : e.src;
        out.emplace_back(begin, begin + e.size);
    }
    std::sort(out.begin(), out.end());
    size_t merged = 0;
    for (size_t i = 1; i < out.size(); ++i) {
        if (out[i].first <= out[merged].second + gap) {
            out[merged].second = std::max(out[merged].second, out[i].second);
        } else {
            out[++merged] = out[i];
        }
    }
    out.resize(out.empty() ? 0 : merged + 1);
    for (auto& r : out) {
        r.second -= r.first;
    }
}

void CopyFilterThread::Check(Job& job) {
    const u64 now_epoch = Epoch();
    if (now_epoch != history_epoch) {
        last.clear();
        ranges.clear();
        history_epoch = now_epoch;
    }
    job.epoch = now_epoch;
    const auto& entries = job.entries;
    hashes.clear();
    keep.clear();
    for (const auto& e : entries) {
        const u64 hash = XXH3_64bits(reinterpret_cast<const void*>(e.src), e.size);
        hashes.push_back(hash);
        const auto it = last.find(e.dst);
        const bool unchanged = it != last.end() && it->second.size == e.size &&
                               it->second.hash == hash &&
                               std::memcmp(reinterpret_cast<const void*>(e.dst),
                                           reinterpret_cast<const void*>(e.src), e.size) == 0;
        if (!unchanged) {
            keep.push_back(u32(hashes.size() - 1));
        }
    }
    total += entries.size();
    left_out += entries.size() - keep.size();
    ++calls;
    // The kept ones are noted. Noted copies never overlap each other: one into
    // exactly a noted destination updates it in place (the ordered map only for
    // new destinations).
    for (const u32 i : keep) {
        const auto& e = entries[i];
        if (const auto it = last.find(e.dst); it != last.end() && it->second.size == e.size) {
            it->second = {e.size, hashes[i]};
            continue;
        }
        auto lo = ranges.lower_bound(e.dst);
        if (lo != ranges.begin()) {
            auto prev = std::prev(lo);
            if (prev->first + prev->second > e.dst) {
                lo = prev;
            }
        }
        while (lo != ranges.end() && lo->first < e.dst + e.size) {
            last.erase(lo->first);
            lo = ranges.erase(lo);
        }
        ranges.emplace(e.dst, e.size);
        last[e.dst] = {e.size, hashes[i]};
    }
    job.kept = u32(keep.size());
    job.control.clear();
    if (keep.size() < entries.size()) {
        job.control.resize(keep.size() * 12);
        for (size_t j = 0; j < keep.size(); ++j) {
            std::memcpy(job.control.data() + j * 12,
                        reinterpret_cast<const void*>(entries[keep[j]].control), 12);
        }
    }
    if (last.size() > 200000) {
        Invalidate();
    }
    static const u64 dest_gap = CopyDestinationGap();
    MergedRanges(entries, true, dest_gap, job.writes);
    MergedRanges(entries, false, 64_KB, job.ranges[0]);
    MergedRanges(entries, true, 64_KB, job.ranges[1]);
    const u32 now = BbStats::coarse_second.load(std::memory_order_relaxed);
    if (now - printed >= 5) {
        static const bool where = bb_env_on("BB_COPY_WHERE");
        if (printed != 0 && where) {
            std::printf("Copy filter (5 s): %llu of %llu copies left out in %llu calls; "
                        "history restarted %llu times; %llu calls ran unchecked\n",
                        (unsigned long long)left_out, (unsigned long long)total,
                        (unsigned long long)calls, (unsigned long long)(now_epoch - printed_epoch),
                        (unsigned long long)unchecked_calls.exchange(0));
        }
        printed = now;
        printed_epoch = now_epoch;
        total = left_out = calls = 0;
    }
}

} // namespace Vulkan
