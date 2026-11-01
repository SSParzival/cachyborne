# Cachyborne 0.8.3 validation

2026-11-01. RX 9070 XT/RADV GFX1201, i5-14600KF, 16 GB RAM; target 2560×1440,
144 FPS cap, existing official FSR 4.1.2 FP8 assets and P/E CPU affinity.
[Structured record](VALIDATION_0.8.3.json).

## Automated checks

- `bash scripts/check-all.sh`: PASS. Native runtime/input/content/network contract
  tests, build with Clang/ThinLTO/mold, **24/24 CTest** targets, **228 Python**
  tests, generated environment documentation, SPIR-V validation and actual RADV
  Vulkan submission/readback. Network contract tests do not contact peers.
- Additional final BC3 reduced-mip regression: PASS on the production Vulkan path.
- `git diff --check`: PASS for this change.

The production download planner and Runtime upload/download are exercised on real
Vulkan images, with different byte patterns for every mip/slice/row and sentinel
bytes in all untouched guest padding and trailing guards. Both compact staging
and direct guest-layout copies are compared byte-for-byte. Cases cover:

| Format/layout | Result |
| --- | --- |
| RGBA8, five mips, three layers, odd dimensions/padded pitch | PASS |
| RGBA8, shrinking 3D volume slices | PASS |
| RGBA8 and BC3, reduced mip chains with original guest offsets | PASS |
| BC1, BC3, BC6H, five mips and three layers | PASS |
| D16 and D32, five mips and three layers | PASS |
| D32S8, independently addressed depth and stencil planes, three layers | PASS |
| Odd stencil slice offsets | Compact path used; invalid direct offset alignment avoided |
| Invalid bounds, level count, guest-address overflow, tiling and MSAA | Rejected safely |

Depth test data stays in the format's valid [0,1] range. The transfer byte-range
regression checks odd compressed extents and stencil's one-byte aspect size.
A separate real-Vulkan clear/copy regression confirms that changing the stencil
allocation also changes the collector's reuse hash. Rebinding the stencil
address invalidates the old baseline and the next GPU write captures the new one.

Runtime tests restore packed rows through the actual C memory API and confirm
shared direct-memory alias visibility, unchanged padding, bad pitches and overflow
rejection. An explicitly unmapped second half of a test allocation verifies that
preflight fails before writing the first row, rather than assuming adjacent
addresses happen to be unmapped. The VMA lock remains held across preflight/store.

Particle tests conserve accumulated time over two simulated minutes at each of
30/60/90/120/144/240/480 FPS. They execute the production x86 stub, including the
original zero-time flag and skipping an incomplete step. Invalid time inputs do
not advance the accumulator. Online-event tests execute the generated native
stub with null and non-null payloads, preserving the original non-null adjustment.
These stubs contain no shared guest code-cave addresses.

## Copied-save game checks

The first offline run lasted **203.3 seconds**, including **180.5 seconds** after
scene draws appeared. The camera rotated periodically without moving the player.
FSR logged **FP8 t2160_m0, output 2560×1440**, material bias **-1.59**, and the
whole-60-Hz particle hook was active. The process stayed alive and exited **0**
on controlled termination. SHA-256 checks confirmed original saves unchanged;
temporary users and input files were removed.

The last ten five-second FPS windows each reported **143.0 FPS** near the cap.
Of 41 RSS samples, those after 60 seconds ranged from **1237640 to 1238508 KiB**;
the observed peak was **1238508 KiB** (about 1.18 GiB). RSS excludes some imported
GPU/shared memory and total system pressure. This stationary-area camera run
neither establishes a general before/after FPS gain nor rules out long-session
leaks, traversal/GC pressure failures or boss-specific particle issues.

A final build/run additionally checks that the online guard's original-code
signature matches the installed 1.09 game and both native hooks are active.
It lasted **47.0 seconds**, including **30.2 seconds** of scene activity, exited
**0**, and confirmed both hooks active and original saves unchanged. This run
also uses the updated runtime row-write implementation. Its structured samples
are retained in the linked record.

## Limits retained

No 4K/ultrawide tests or tuning were performed for this release. Tiled/MSAA GC
writeback and ambiguous multi-mip stencil descriptions are not enabled. A VMA
transaction is not atomic exclusion against raw guest CPU stores or heap reuse.
The online smoke runs offline: a generated null-event regression does not certify
network transport/peer interoperability. Lamp-travel pressure loops and targeted
boss comparisons remain separate validation work; no “perfect in every scene”
claim follows from these results. See [remaining scope](ROADMAP.md).
