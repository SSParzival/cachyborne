# GPU command, draw and submission pipeline

## Two-stage draw pipeline

`gpu/cp/` decodes the guest PM4 stream. Stage A on the GPU command thread owns the
register file and pipeline selection. It publishes draw packets containing
changed registers, user data, constants and draw arguments through the bounded
ring in `gpu/render/vk_draw_pipe.h`.

Stage B on the draw-recording thread consumes packets in order, owns the cache/
barrier/recording state while packets are in flight, applies its register copy
and records the draw. A stage-A operation that needs that mutable state first
uses `DrainDrawPipe`. Resolve helpers can copy/prepare packet data using their
own rings; ownership and retirement follow completed packet positions.

`gpu/render/raster_pipe.cpp` packages packets and handles lifetime checks.
`gpu/render/scheduler.h` records closures in bounded chunks, can record segments
into separate command buffers and submits them in stream order. Copied descriptor
or vertex arrays must outlive the closure that records their command; all grouped
`RecordData` callers must reserve capacity together. Reducing a chunk margin alone
is not a demonstrated lifetime fix.

## Fences and retirement

`gpu/render/raster_signals.cpp` publishes guest signals after the GPU completes
the preceding work. Timeline ticks also control deferred resource destruction.
The recording thread, command thread and signal thread must not form a cycle
while handling a protected guest-memory read. Backing reads used by hashing avoid
asking the GPU thread to perform a readback while it waits for those same copies.

`SubmitForSignalsNow` batches work and considers idle/draining GPU state and
recording backlog. It is not Supermedo's optional per-draw `MaybeSubmitEarly`
mechanism. A vendor guard for that absent feature must not disable required local
fence submissions. Device choices must use the selected physical device and actual
Vulkan features rather than a launcher's first enumerated adapter.

## CPU placement, memory and diagnostics

Cachyborne's explicit CPU masks own host placement. On the target PC foreground
command/record/simulation/audio threads use P CPUs 0–11; named shader compilation,
import/rebuild and decompression workers use E CPUs 12–19. The original allowed
mask is retained so placement never widens an external cpuset restriction.

Buffers, textures and staging have independent lifetimes and budgets. Reusing a
Vulkan image allocation does not establish ownership of its guest address.
Collector writeback checks a full-range CPU baseline established at binding or
GPU copy/clear/resolve; it remains a heuristic against concurrent reuse, not an
allocation-generation lock. No image-sized shadow is retained by that guard.

Use existing GPU profiles/timelines and narrowly scoped diagnostic variables
from [the environment catalog](ENVIRONMENT.md) for comparisons. Record scene,
resolution, driver, warm/cold cache and memory pressure. Synthetic pass times do
not establish whole-game FPS or end-to-end input latency. Keep profiling off for
ordinary play and preserve the normal stream-order/fallback paths.

[Architecture](../RESEARCH_AND_ARCHITECTURE.md) · [community decisions](COMMUNITY_AUDIT_0.8.2.md)
