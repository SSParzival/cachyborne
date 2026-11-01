# Community review — Cachyborne 0.8.2

Review date: 2026-11-01. Stable base: deadinside28/bloodborne_pc 0.7,
`0fc2921c45f8ff3900f6bfa43a299e9a4f532af4`. The received working tree already
contained a substantial uncommitted 0.7/0.8.x integration. It was preserved,
reviewed by subsystem and subjected to the full suite before further changes.

The six requested community refs were fetched again without moving HEAD or
merging branches. **166 additional commits were inventoried** relative to the
previous ref tips. Source inspection focused on relevant diffs and their local
callers; neither this inventory nor the older fork count means a line-by-line
review of every launcher, asset or networking subsystem in the ecosystem.
The [machine-readable provenance](COMMUNITY_AUDIT_0.8.2.json) records full SHA,
authors, changed files, previous sources and exact refreshed tips. No blind
cherry-picks or speculative driver defaults were applied.

## Follow-up changes selected and tested

| Source | Mechanism and local adaptation | Target-machine effect |
| --- | --- | --- |
| ayozetr `65fad4c5`, `28bb6331`, `df102802`, `1bc4f375` | One full-range streaming XXH3 for dirty marking, refresh and collector ownership. Compare guest bytes before restoring old GPU images. Resolve mere `MaybeCpuDirty` suspicion before deciding download/eviction. | 4 KiB scratch plus hash state; no retained host image duplicate. Adds CPU reads only on the corresponding checks; no new Vulkan submissions or threads. |
| ayozetr `12d76b38` | Move baseline capture into Image and call it from the shared `Runtime::MarkCopied` path, covering copies, buffer-mediated copies, mip copies, depth copies, clears and resolves as well as shader/target bindings. | The earlier port failed closed for missing ownership, but could discard valid copied/cleared GPU images under pressure. Complete capture avoids those unnecessary drops without permitting unowned writeback. |
| ayozetr `6530aa16`, originally Lantern | Require finite camera matrices; replace NaN/Inf motion with zero and clamp extreme camera vectors to four render extents. | Prevents a bad frame from poisoning FSR/TAA history. No persistent extra allocation. Production shader tests cover non-finite/extreme inputs. |
| ayozetr `4a001fa5`, originally nick24051998 / Supermedo `257d6bbf` | Branch on masked null SRT pointers and zero all flattened words of that subtree, including children. Restore the parent pointer on both paths. | Avoids a signal per null-table draw. Generated-code regression executes root-null, child-null and later-valid cases, preserving adjacent data. Non-null invalid-pointer recovery remains. |

All baseline capture uses `ReadBacking`, avoiding recursive read traps on GPU
threads. It is restricted to collector-writable linear, single-sample images;
the local collector still cannot retile GPU-only tiled images. A hash can collide
and does not exclude guest writes between checking and restoring: this is a
useful guard, not proof of atomic ownership or absence of all memory corruption.
No GC budgets, memfd limits or P/E masks were increased to accommodate the port.

## Previously requested commits: disposition after re-review

| Commit(s) | Decision and collision analysis |
| --- | --- |
| Supermedo `453c6c57` | Its optional per-draw early-submit path does not exist locally. Do not disable `SubmitForSignalsNow`, which publishes required fences and has different idle/draining policy. The cited Radeon timings are the fork author's measurements, not a reproduced RADV benchmark. No fake Midnight switch is added. |
| Supermedo `e149ab89`, `33ffd3a5` | Latency-per-line HUD and colors remain candidates. Local LowLatency supplies markers, not an equivalent defined render-latency readout. Implement a nonblocking bounded measurement API before Values/Load/Save/schema/INI_OPTIONS/Midnight controls. Do not label reciprocal FPS as latency. |
| Linux `65fad4c5`, `28bb6331`, `df102802`, `55f9db99` | Ownership and dirty-state corrections integrated as above. DynamicIndex mip-binding counting was already adapted across Info/layout/raster and is tested; do not duplicate it. Tiled writeback itself is not enabled. |
| Linux `42ec043c` | Full-mip download remains critical separate work: preserve only written rows/slices, padding, BC blocks, in-place mirrors, reduced images and depth/stencil planes. Copying the fork's whole staging block over guest padding is not accepted as a complete port. |
| Linux `8d18daeb` | Changes the default to a direct camera; local input already calls `bbgpu_camera_look` with stick/free-camera fallback. Preserve existing sensitivity/inversion controls and user preferences. |
| Deck `930c88f6`, `9c3036a8`, `c65c1bd9` | Packaging, an optimization handoff and an anchor correction. No GPU texture-copy optimization is implemented by these commits. `znver2` tuning does not replace this machine's native Clang build. |
| Mrsuss `a7cbfae6` | Do not overlay another clock, code caves and delta smoothing on `bbport_frame_rate.cpp`, which already handles live time/cloth/animation/wall checks. A replacement requires exact write-site signatures and scene tests, not merely a new FPS menu. |
| Mrsuss `f8f450b1` | BC6H no-storage fallback already exists. Do not reduce recording reserves from 1024 to 256 bytes without lifetime tests for all grouped RecordData callers. Batched readback needs a staging-byte budget in 16 GB. VEH/regional exception paths are Windows-specific. |
| Mrsuss `466e8e7d` | Separate forward-Z depth filtering from global TAA tuning and descriptor identity. A fixed 2×2 footprint is not generally conservative for every scale/reverse-Z; erasing buffer base addresses can merge unrelated object histories. No blanket clarity preset is imported. |
| Lantern `a4719168` | Its SDK loader uses Win32 DLL loading and returns unavailable on Linux. The new Linux asset workflow uses official 4.1.2 during offline extraction and runs SPIR-V directly; it does not pretend the Windows loader is native. |
| Lantern `ad2d8161`, `08af891e`, `b112d5a4` | Room messages, party travel, progress, portraits and revive-body tracking require Lantern's transport/state contracts, absent from local BBSC/shadNet. Defer an interoperable subsystem port; do not add boolean options without a backend or change server/save semantics. |
| Runtime `8bc0ce68` | Persistent permutation-index reservations already exist locally, with bounds and cache versions appropriate to this base. Regression retained. |
| Runtime `3c7140f6` | Documentation of measurements on Metal, not an occlusion code patch. Local Vulkan query intervals and fallback already exist. No extrapolation from Metal to RADV. |

## Further findings in the refreshed branches

- Linux `5cc279af`, `916bcd1c`, `8ffbf6b5`, `3ce0dde3`, `6650b7dc` and `562bb095`
  describe texel-buffer miss memoization, per-layer clears, protected-memory
  rejection, cross-chunk reads and occlusion improvements already represented
  in the local 0.7 integration. The FSR memory/shader-hook modifications are
  versioned patches against the pinned backend, not unpublished submodule commits.
- Linux `a9f292a7` introduces effects stepping in whole 1/60-second units to avoid
  particle accumulation. This remains interesting: local frame hooks already
  redirect the effect time read at `0x1f77d4a`, so importing its XML/code cave
  without instrumenting that clock would combine different time policies.
  It needs a targeted particle-expiration/Ebrietas comparison.
- Linux `850c7869` and `ad24c9f0` modify cloth/foliage and air drag. The local
  frame-time implementation already computes exponential decay at the actual
  step and adapts cloth constraints. Keep one owner of those constants/hooks;
  the new patch set needs collision and trajectory tests before replacement.
- Linux `eec77ba2` adds an online-event null-data guard in a shared code cave.
  Review the local installed hooks/cave occupancy and build exact signature
  fixtures before importing; it is not a reason to replace the active network
  transport. `a0f120f5` radial deadzones likewise need adaptation to local calibrated
  axis/outer-gate behavior; no controller preference is silently migrated.
- Mrsuss `624ac749` dense VRAM mirrors was reverted by `da428934`; it is not a
  selected memory optimization. `c3f55890` also includes WASAPI/timer policy and
  audio gain/pacing changes: preserve the local Linux audio/voice path and P-Core
  policy rather than importing a Windows low-core profile. `6b2cc0e0`/`b62051db`
  span packet pacing and shader lowering; compare with the iterative local CP,
  existing packet decoder tests and cache identity before any independent port.
- Lantern `4889a8a8` adds many lighting passes/resources (roughly 8800 changed
  lines), and newer commits extend room networking/voice/progress. These are
  substantial candidates, not small verified gains for the 16 GB profile.
  `12614252` concerns its own hook writer; the local hook implementation differs.
- Runtime's new shader-list/pipeline work targets its renderer/recompiler and
  Metal device states, including explicit Metal-only filtering. Do not import
  device-state recipes as if they were ready Vulkan pipeline layouts. Existing
  local compilation workers retain E-Core placement.
- Supermedo's additional changes mostly concern its Windows launcher, account
  lifecycle, atomic writes, imports, updates and graphics heuristics. Midnight
  already publishes prepared generations atomically and preserves local servers.
  Neither the fork's account policy nor removal of a provider determines this
  repository's configuration.

These candidates are recorded rather than described as worthless or already
implemented. The inventory is a time-bounded review, not a claim that no future
fork change can improve the project.

## Additional recent-fork screen

The 100 newest forks were screened by activity; eight additional recently active
heads were compared directly with the stable base. EthanBlazkowicz, Just-a-Spider,
ProfessorTee and JOmega321 were identical to 0.7. Fabioesantos' musl support changes
non-glibc allocation/mutex/function-name portability, not this CachyOS/glibc path.
Heus-Sueh replaces the runtime/launcher architecture with C++23/TOML/VFS; it is a
separate migration, not a small measured renderer improvement. Godlyfast's English
UI, FFX backend hooks and clear-format/host-handle fixes overlap local work; its
NVIDIA profile does not justify changing the RADV defaults.

Pera (`CharlExMachina/pera-bloodborne`) provided two selected graphics corrections:

- `e40711cd6ad7fd19093e3565bbf7656421d1ee79`, Carlos Eduardo Pérez Villanueva:
  FSR material mip bias is `log2(render/output)-1`, including -1 at Native AA.
  The extra offset is restricted to the FSR backends; TAA/spatial paths retain
  their behavior and DLSS keeps its ratio bias. Only scene-material samplers
  change; no additional image-sized CPU allocation or new thread is created.
  Higher-detail sampling can increase GPU texture bandwidth and is not advertised
  as an FPS optimization.
- `b5d7ae51c3d4174083a07081f834a2cca39ac785`, same author: measure camera translation
  and angle between normalized view axes; movements over 2 world units or 45° in
  one frame reset reconstruction history. Both local motion-recording paths use
  the result, and the flag clears at the display boundary. Existing reset logic
  handles the actual FSR/TAA buffers. Production math tests cover normal motion,
  a jump, a 90° rotation and non-finite input.

Pera's larger placement/ground-truth instrumentation and native-output scene
allocation strategy are not imported: forcing the game's 1440p scene allocations
would conflict with the bounded direct-memory profile. This review remains
bounded by the recorded ref heads and does not claim an exhaustive proof over
all forks or all future commits.

## FSR and release integration

The official [AMD SDK 2.4.0 release](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/releases/tag/v2.4.0)
contains upscaler **4.1.2.4555**. The downloaded file's Git blob identity was
checked against AMD's tagged tree, and the reusable downloader pins SHA-256
`aad4fccccb29084a655f9a4f9f613bd22c8c03fb4a0f8840148cc6543b4ccace`.
Generated models/dispatches were checked against replay rules; the native FP8
path was compared with the DLL on the target GPU. See [validation](VALIDATION_0.8.2.md)
for the exact cases and differences. No proprietary DLL or generated model is
committed. Source provenance and user configuration remain distinct.

The reviewed release is assembled in thematic commits for runtime/GPU foundation,
FSR tools/assets configuration, native localization/icons and current docs/tests.
All received changes are included or explicitly replaced by reviewed equivalents;
private configuration, saves, game files, caches and generated binaries stay out
of Git. [Release notes](RELEASE_0.8.2.md) describe supported behavior and remaining work.

FSR replay builders are now named `bb:FsrBuild` and classified as background
compilation workers by the existing affinity policy. They can move from their
creator's P-Core mask to the permitted E-Core mask; the real-affinity regression
covers this role. Rendering, audio and simulation placement remain unchanged.
