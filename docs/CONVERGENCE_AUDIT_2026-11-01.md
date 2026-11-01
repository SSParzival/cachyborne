# Performance convergence audit — 2026-11-01

Starting checkout: `41b4bb66`. Target verified locally: i5-14600KF, 20 logical
CPUs, 16 GB RAM, RX 9070 XT / RADV GFX1201, Mesa 26.2.5-arch3.1, Vulkan 1.4.354,
CachyOS kernel 6.18.55-2-cachyos-lts, Clang 23.1.4. This is an incremental audit
of an already substantially adapted bbport 0.7 tree. It does not establish a
fastest-port ranking or certify every scene, physics interaction or travel route.

## Source snapshots and comparison

The following public heads were fetched for this review. Community histories
and the earlier [0.8.2](COMMUNITY_AUDIT_0.8.2.md) and
[0.8.3](COMMUNITY_AUDIT_0.8.3.md) provenance records remain applicable.

| Project | Inspected revision | Relevant source |
| --- | --- | --- |
| Lantern Engine | [`e6b943dd969d99561545315f3d801f10e4a6cf8d`](https://github.com/Awakenchan/LanternEngine/tree/e6b943dd969d99561545315f3d801f10e4a6cf8d) | `engine/gpu/shim/engine/{frame_rate.cpp,camera_timing.cpp}`, `engine/gpu/shadps4/video_core/texture_cache/texture_cache.cpp`, `renderer_vulkan/vk_contact_shadow.cpp`, `host_shaders/contact_shadow.comp` |
| Supermedo | [`b48052617a919fc4d6b10ae3395e209c9e94e17a`](https://github.com/Supermedo/bloodborne_pc/tree/b48052617a919fc4d6b10ae3395e209c9e94e17a) | `gpu/shadps4/video_core/renderer_vulkan/vk_pipeline_serialization.cpp`, `launcher/bbport_i18n.py`, related release history |
| ayozetr Linux | [`b27d0715b897486b6115a34b801661714c0a988f`](https://github.com/ayozetr/bloodborne_linux/tree/b27d0715b897486b6115a34b801661714c0a988f) | Particle/online-guard commits and all-mip/ownership fixes documented in the previous audit; current head matches the previously fetched community ref |
| bbport 0.7 | [`0fc2921c45f8ff3900f6bfa43a299e9a4f532af4`](https://github.com/deadinside28/bloodborne_pc/tree/0fc2921c45f8ff3900f6bfa43a299e9a4f532af4) | Stable base; native pipeline/import architecture |
| shadPS4 | [`8a66adabf74072e24162b098d7bfd3156b475393`](https://github.com/shadps4-emu/shadPS4/tree/8a66adabf74072e24162b098d7bfd3156b475393) | `src/video_core/texture_cache/texture_cache.cpp`, `src/video_core/renderer_vulkan/vk_instance.cpp` |

Mesa's [util_queue implementation](https://gitlab.freedesktop.org/mesa/mesa/-/blob/main/src/util/u_queue.c)
explicitly supports restoring the startup affinity before naming a worker.
Its [disk cache queue](https://gitlab.freedesktop.org/mesa/mesa/-/blob/main/src/util/disk_cache.c)
requests this behavior and uses the `disk$` queue name. Consequently, parent-thread
pinning alone does not place those workers on E cores. Source snapshots used
for inspection are retained under `out/convergence-mesa-*.c`.

Lantern's camera timing hooks are enabled only by `LANTERN_CAMERA_TIMING=1` in
the inspected source. Its rate substitutions and Havok integration differ from
Cachyborne's measured-delta hooks; applying both to the same sites would double
scale time. Its effect-step policy also differs from the whole-60-Hz accumulator
already present locally. No second set of timestep patches was installed.

Lantern's collector explicitly limits expensive tiled stores to four per pass,
backs off failed stores, and protects recent submissions. It does not simply
flush every texture on a lantern transition. Cachyborne retains its conservative
layout/reuse guards. A remaining bounded-retention issue was addressed in the
staging pool, as described below; this is not a claim that all travel stutter has
been eliminated.

Contact shadows and PCSS in Lantern use depth/shadow-map inputs, additional
storage bindings, blocker/filter sampling loops and compute passes. Their source
also depends on Lantern's scene/resource tracking and newer lighting pipeline.
These are not free toggles on Cachyborne's renderer. They were not copied or
enabled without a compatible integration and before/after GPU timing. Existing
GT-VBAO and optional SSR remain available; no unimplemented POM control was added.

Supermedo's pipeline serialization preloads cached recipes, checks versions and
build fingerprints, and builds pipelines in parallel. Cachyborne already has
fingerprinted pipeline storage, asynchronous compilation and shader-recipe import
in `gpu/render/{pipeline_store,pipeline_import,pipeline_cache}.cpp`. No portable
first-run shader corpus can be manufactured from an empty cache. No new universal
prebuilt cache is shipped. The inspected Python localization catalog uses Russian
keys and English values; it is not a complete Spanish game-text source. The
Spanish additions here are independently written for local English message keys.

Upstream shadPS4 downloads every mip; Cachyborne already plans every resident
linear mip/layer/volume slice, preserves padding, handles separate stencil, and
refuses unsupported layouts. Its Vulkan tests exercise this production path.
The existing `0x1eb3de8` null-event guard and `0x1f77d48` effect hook remain intact.

## Implemented changes

### Bounded parallel asset validation

`dcx_broken` previously loaded the entire compressed file and expanded stream
into memory. It now streams 64 KiB input blocks and at most 256 KiB output per
inflate call, discarding expanded data after counting it. Truncated DCX headers,
missing stream terminators/checksums, forged lengths, checksum failures and
trailing data inside the declared compressed range are rejected. Padding outside
that range is permitted; unsupported complete compression formats remain unchecked.

`checked_dcx` limits the pool to eight workers and the pending queue to twice the
worker count, preserving ordered diagnostics. Python zlib releases the GIL during
inflate. Workers use the configured E-core set intersected with the calling
thread's allowance when `BB_CPU_PINNING=1`; they never widen a taskset/cgroup.
Preparation invokes this path for the full DCX inventory, and shader validation
uses the same checker.

This accelerates preparation/validation. It does **not** replace the guest game's
runtime decompressor or claim shorter in-game lantern loads. Installing a native
libdeflate guest hook still requires a verified ABI, buffer ownership and lifetime
contract. No speculative address hook was added.

### Hybrid affinity

Vulkan/SDL initialization now starts on P cores. A single idempotently started
monitor checks library workers every 250 ms: recognized Mesa `process:disk$N` and
`process:shN` names select E cores; audio/FMOD names select P cores. Unknown library
threads retain their own masks. Port-owned workers still receive placement at
thread entry, using the full role name before Linux's 15-character truncation.

The initial allowed CPU set remains the upper bound. Invalid/empty selections
fail without widening masks. No realtime policy, priority boost or scheduler
replacement is installed. Library placement is periodic reconciliation, **not**
a guarantee that an externally reset thread never briefly runs on another core.
The production monitor test creates actual threads that reset inherited masks
and verifies correction while an unrelated library thread remains untouched.

### Staging and Vulkan completion

`BB_STAGING_HOST_IDLE_MS=30000` adds a monotonic idle deadline alongside the
existing 3000-frame policy. At 30/60 FPS, frame counting alone retained host
loading/readback peaks for 100/50 seconds. Completed, unheld whole buffers and
eligible segments can now retire after 30 seconds. Upload floors and the active
segment remain reusable; unfinished submissions and held deferred readbacks remain
protected. `0` restores frame-only host aging. This bounds idle lifetime, not the
peak capacity required by in-flight work, and collection still runs at TickFrame.

A regression exposed an independent completion-cache issue: `Semaphore::Wait`
called the throttled `Refresh` after a successful driver wait. That could leave
`IsFree` stale until the next poll. The implementation now atomically raises the
known tick to the confirmed value, preserving newer concurrent observations.
This follows the [Vulkan wait contract](https://docs.vulkan.org/refpages/latest/refpages/source/vkWaitSemaphores.html).
It adds no driver query. The device regression throttles polling to one second
and requires immediate visibility of successful completion before testing reclamation.

Sparse memfd hole punching, imported-page pin protection, the 5056 MiB guest
budget and existing compact valid-row writeback remain in place. Replacing shared,
GPU-imported backing with unconditional MADV_DONTNEED/MADV_FREE would not preserve
that ownership contract. No tmpfs asset copy or large anonymous cache was added.

### Spanish interface

The current 388-key English catalog now has Spanish coverage for every key;
355 entries were previously absent. Current literal translation call sites and
35 native setup/validation errors are covered as well. Midnight selects Spanish
for these diagnostics, and all six game-validation failures have Spanish recovery
instructions. Tests check lookup coverage, format fields, line breaks and native
validation messages. Unknown external diagnostics remain visible unchanged.

In-game item descriptions, subtitles and ordinary game menus use the retail
Spanish `item`/`menu` archives from the user's dump. They are not copied from a
fork or redistributed. Existing language validation checks their availability;
Spanish text and English/Japanese voices remain separate controls. Existing
port-owned overlay/native-menu Spanish catalogs and Sweet Purple desktop assets
are preserved. The new catalog does not translate third-party mod text or arbitrary
operating-system/driver error messages.

## Validation

See [structured results](CONVERGENCE_VALIDATION_2026-11-01.json) for measurements
and source revisions. Logs and private game captures live under `out/` and are
excluded from Git. No game data, model weights or saves were added to commits.

- `bash build.sh` and `bash scripts/check-all.sh`: Clang native/ThinLTO/mold build,
  native runtime/input tests, 26/26 CTest tests, 239 Python tests, SPIR-V validation,
  environment registry and actual RADV command submission/readback pass.
- Unfiltered `ctest --test-dir out/gpu --output-on-failure`: 26/26 pass.
- Deterministic staging regression repeated 20 times after the timeline fix: pass.
- Live PipeWire: 2000 silent 256-frame buffers at 48 kHz; both reported windows
  had `starved 0`, maximum observed gap 5.39 ms. This is a device smoke test,
  not a guarantee about audio during combat or cinematics.
- All 26,913 retail DCX files passed. The streaming parallel scan took 38.11 s,
  peak RSS 53,880 KiB, no swaps. The original serial checker also passed, took
  151.60 s and peaked at 258,384 KiB. Runs were sequential, with uncontrolled
  filesystem caching and other validation activity: these timings are observations,
  not a controlled speedup ratio or an in-game loading-time benchmark.
- Official SDK 2.4.0 / upscaler 4.1.2.4555 assets ran the native FP8 replay at
  1706×960 → 2560×1440 Quality for 900 synthetic frames. Three dispatch windows
  measured 0.720, 0.668 and 0.672 ms/frame. This is nonzero GPU work; it is not
  end-to-end latency or full-scene FPS.

Copied-save scene runs at 60/144 FPS additionally verify presentation, visible
GPU output, FP8 replay and actual thread masks. These are stationary short runs,
not traversal, boss physics, online play or repeated lantern-warp acceptance tests.
The structured record distinguishes final runs from earlier exploratory runs.
The user's configured profile link pointed to a missing prepared generation;
validation instead prepared an isolated profile under `out/convergence-scene`.
The personal profile link, preferences and original saves were not replaced.

Final runs used the rebuilt timeline fix and the SDK 2.4.0/4.1.2.4555 assets,
with 5056 MiB guest direct memory and no frame generation. Both exited 0 after
controlled termination; checksums confirmed the original saves unchanged.

| Cap | Last five FPS windows | Gameplay frame samples | Median / p99 | Peak process RSS |
| --- | --- | --- | --- | --- |
| 60 | 59.8 each | 1684 | 16.72 / 16.73 ms | 1,246,476 KiB |
| 144 | 143.0, 142.8, 143.0, 143.0, 143.0 | 3971 | 7.00 / 7.01 ms | 1,243,048 KiB |

Runs lasted 57.44 and 52.45 seconds respectively, with at least 35 seconds after
a gameplay scene was detected. Frame samples omit early loading/warm-up and
incomplete final CSV rows. Process RSS does not include all imported/shared GPU
memory. Logs confirm FP8 reconstruction and 2560×1440 host composition; the
nonblank raw diagnostic captures themselves are 1920×1080. A snapshot at the end
of each run places GPU command/recording and audio roles on `0-11`, shader/cache
workers on `12-19`. These observations do not prove a gain over the starting code.

## Remaining acceptance work

Repeated lantern travel with VRAM/RSS tracking, jump-distance/collision/slope/cloth
comparisons against 30 FPS, long boss particle runs, and controlled input-to-display
latency measurements remain unverified. New Lantern lighting/POM integration and
a runtime libdeflate asset hook are not implemented by this change. No benchmark
here establishes universally stable 60/144 FPS or zero loading/pipeline overhead.
