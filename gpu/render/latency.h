// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
extern "C" {
extern int bbgpu_latency_enabled;
void bbgpu_latency_input_sample(void);
}
namespace BbLatency {
using u64 = std::uint64_t;
enum class Stage : unsigned {
    Input,
    GuestSubmit,
    QueueSubmit,
    GpuObserved,
    PresentReturned,
    Count
};
struct Sample {
    u64 id = 0;
    std::array<u64, 5> ns{};
};
// Bounded, multi-writer instrumentation. try_lock never waits on another thread;
// contended or overwritten telemetry is dropped, never allowed to stall rendering.
class Ring {
    struct Slot {
        std::atomic_flag lock = ATOMIC_FLAG_INIT;
        Sample value{};
        bool published = false;
    };
    std::array<Slot, 128> slots{};
    std::atomic<u64> next{1}, lost{0};

  public:
    u64 Begin(u64 input, u64 submit);
    bool Mark(u64 id, Stage stage, u64 ns);
    bool Take(u64 id, Sample &sample);
    u64 Dropped() const { return lost.load(std::memory_order_relaxed); }
};
struct Summary {
    double render_upper_ms = 0, input_age_ms = 0, jitter_ms = 0;
    u64 samples = 0, dropped = 0;
};
inline bool Enabled() { return bbgpu_latency_enabled != 0; }
void Initialize();
bool Hud();
void GuestSubmit();
u64 RenderFrame();
void EndRenderFrame();
void Mark(u64 id, Stage stage);
void Complete(u64 id);
bool ReadSummary(Summary &);
} // namespace BbLatency
