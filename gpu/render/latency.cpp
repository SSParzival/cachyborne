// SPDX-License-Identifier: GPL-2.0-or-later
#include "latency.h"
#include "common/thread.h"
#include "shim/bb_env.h"
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <thread>

extern "C" {
int bbgpu_latency_enabled = 0;
}
namespace BbLatency {
u64 Ring::Begin(u64 input, u64 submit) {
    const u64 id = next.fetch_add(1, std::memory_order_relaxed);
    auto &slot = slots[id % slots.size()];
    if (slot.lock.test_and_set(std::memory_order_acquire)) {
        lost.fetch_add(1);
        return 0;
    }
    if (slot.value.id && !slot.published)
        lost.fetch_add(1);
    slot.value = {id, {input, submit, 0, 0, 0}};
    slot.published = false;
    slot.lock.clear(std::memory_order_release);
    return id;
}
bool Ring::Mark(u64 id, Stage stage, u64 ns) {
    if (!id || unsigned(stage) >= unsigned(Stage::Count) || !ns)
        return false;
    auto &slot = slots[id % slots.size()];
    if (slot.lock.test_and_set(std::memory_order_acquire)) {
        lost.fetch_add(1);
        return false;
    }
    const bool valid = slot.value.id == id;
    if (valid && !slot.value.ns[unsigned(stage)])
        slot.value.ns[unsigned(stage)] = ns;
    slot.lock.clear(std::memory_order_release);
    return valid;
}
bool Ring::Take(u64 id, Sample &sample) {
    if (!id)
        return false;
    auto &slot = slots[id % slots.size()];
    if (slot.lock.test_and_set(std::memory_order_acquire))
        return false;
    const auto &v = slot.value;
    // T4 is a CPU observation of GPU completion; it may follow queuePresent's
    // return (T5). Never force a false T4<=T5 ordering or call T5 scanout.
    const bool ready =
        v.id == id && !slot.published && v.ns[2] && v.ns[3] >= v.ns[2] && v.ns[4] >= v.ns[2];
    if (ready) {
        sample = v;
        slot.published = true;
    }
    slot.lock.clear(std::memory_order_release);
    return ready;
}
namespace {
u64 Now() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
Ring ring;
std::atomic<u64> latest_input{0}, submitted_input{0}, latest_submit{0}, active{0};
bool hud = false;
struct Trace {
    std::array<Sample, 256> queue{};
    std::atomic<u64> head{0}, tail{0}, lost{0};
    FILE *file = nullptr;
    std::jthread writer;
    ~Trace() {
        if (writer.joinable()) {
            writer.request_stop();
            writer.join();
        }
        if (file)
            std::fclose(file);
    }
    void Start(const char *path) {
        if (!path)
            return;
        file = std::fopen(path, "wx");
        if (!file) {
            std::fprintf(stderr, "Latency: cannot create trace %s\n", path);
            return;
        }
        std::fputs("frame,input_poll_ns,guest_submit_ns,queue_submit_ns,gpu_observed_ns,present_"
                   "return_ns\n",
                   file);
        writer = std::jthread([this](std::stop_token stop) {
            Common::SetCurrentThreadName("bb:PipeCacheIO");
            do {
                u64 read = tail.load(std::memory_order_relaxed),
                    end = head.load(std::memory_order_acquire);
                for (; read != end; ++read) {
                    const auto &s = queue[read % queue.size()];
                    std::fprintf(file, "%llu,%llu,%llu,%llu,%llu,%llu\n", (unsigned long long)s.id,
                                 (unsigned long long)s.ns[0], (unsigned long long)s.ns[1],
                                 (unsigned long long)s.ns[2], (unsigned long long)s.ns[3],
                                 (unsigned long long)s.ns[4]);
                }
                tail.store(read, std::memory_order_release);
                std::fflush(file);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            } while (!stop.stop_requested() || tail.load() != head.load());
        });
    }
    void Push(const Sample &sample) {
        if (!file)
            return;
        const u64 next = head.load(std::memory_order_relaxed);
        if (next - tail.load(std::memory_order_acquire) >= queue.size()) {
            lost.fetch_add(1);
            return;
        }
        queue[next % queue.size()] = sample;
        head.store(next + 1, std::memory_order_release);
    }
} trace;
std::atomic<u64> version{0}, render_ns{0}, input_ns{0}, jitter_ns{0}, count{0};
u64 previous_age = 0;
double jitter = 0;
} // namespace
void Initialize() {
    static std::once_flag once;
    std::call_once(once, [] {
        hud = bb_env_on("BB_LATENCY_HUD");
        const char *path = bb_env("BB_LATENCY_TRACE");
        bbgpu_latency_enabled = hud || path;
        if (Enabled()) {
            trace.Start(path);
            std::puts("Latency: software timestamps; GPU completion is CPU-observed; present "
                      "return is not physical scanout");
        }
    });
}
bool Hud() { return hud; }
void Input() {
    if (Enabled())
        latest_input.store(Now(), std::memory_order_relaxed);
}
void GuestSubmit() {
    if (Enabled()) {
        submitted_input.store(latest_input.load(std::memory_order_relaxed),
                              std::memory_order_relaxed);
        latest_submit.store(Now(), std::memory_order_relaxed);
    }
}
u64 RenderFrame() {
    if (!Enabled())
        return 0;
    u64 id = active.load(std::memory_order_relaxed);
    if (!id) {
        id = ring.Begin(submitted_input.load(std::memory_order_relaxed),
                        latest_submit.load(std::memory_order_relaxed));
        active.store(id, std::memory_order_relaxed);
    }
    return id;
}
void EndRenderFrame() {
    if (Enabled())
        active.store(0, std::memory_order_relaxed);
}
void Mark(u64 id, Stage stage) {
    if (Enabled() && id)
        ring.Mark(id, stage, Now());
}
void Complete(u64 id) {
    if (!Enabled())
        return;
    Sample s;
    if (!ring.Take(id, s))
        return;
    trace.Push(s);
    const bool input_valid = s.ns[0] && s.ns[0] <= s.ns[1] && s.ns[1] <= s.ns[2];
    const u64 age = input_valid ? s.ns[4] - s.ns[0] : 0;
    if (age) {
        if (previous_age)
            jitter += .1 * (std::abs(double(age) - double(previous_age)) - jitter);
        previous_age = age;
    }
    version.fetch_add(1, std::memory_order_acq_rel);
    render_ns.store(s.ns[3] - s.ns[2], std::memory_order_relaxed);
    input_ns.store(age, std::memory_order_relaxed);
    jitter_ns.store(u64(jitter), std::memory_order_relaxed);
    count.fetch_add(1, std::memory_order_relaxed);
    version.fetch_add(1, std::memory_order_release);
}
bool ReadSummary(Summary &s) {
    const u64 before = version.load(std::memory_order_acquire);
    if (before & 1)
        return false;
    s = {double(render_ns.load()) / 1e6, double(input_ns.load()) / 1e6,
         double(jitter_ns.load()) / 1e6, count.load(), ring.Dropped() + trace.lost.load()};
    return before == version.load(std::memory_order_acquire) && s.samples;
}
} // namespace BbLatency
extern "C" void bbgpu_latency_input_sample(void) { BbLatency::Input(); }
