# Cachyborne 0.8.2 validation

Date: 2026-11-01. Host: i5-14600KF, 16 GB RAM, RX 9070 XT/GFX1201,
Mesa RADV 26.2.5-arch3.1, Vulkan 1.4.354. Actual topology confirms P CPUs
0–11 and E CPUs 12–19. [Structured results](VALIDATION_0.8.2.json).

## Build and regression suite

| Check | Result |
| --- | --- |
| Received integration, before follow-up changes | Full `check-all.sh` PASS; 228 Python tests |
| Final `bash scripts/check-all.sh` | PASS, exit 0; runtime C tests, GPU build, CTest, Python, SPIR-V validation and real Vulkan smoke |
| CTest `-L cachyborne` | 21/21 PASS |
| Python full suite in the desktop session | 228/228 PASS |
| Python without DISPLAY/WAYLAND_DISPLAY | PASS, 228 tests, one real-GUI test skipped |
| Standalone `fsr4-bench` build | PASS; fixed its missing root include directory and added compilation to check-all |
| FSR backend patch reproducibility | All three versioned patches applied to pristine pinned `c64f093`; result matches the working backend exactly |
| Shell/Python syntax and environment registry | PASS; generated environment documentation current |
| Source whitespace check | PASS excluding unified patch-context whitespace in `gpu/bbnet20/vendor-changes.diff` |
| Credential pattern scan of maintained source | No candidate private-key/token files found |

Compiler warnings from existing shader headers about unused parameters and
initializer order remain; the build does not treat those as validation of their
semantics. No new warning suppression or global fast-math option was introduced.

New/extended regressions execute the production streaming hash against independent
one-shot XXH3 at chunk boundaries and at the end of a large range; real Vulkan
clear/copy calls capture/retain/restart their ownership baselines; generated SRT
code handles root-null, child-null and later-valid data; camera motion rejects
NaN/Inf and bounds extreme output. Existing shader-index, mip-binding, phi,
serialization, CPU affinity (including the FSR context-build role), memory, audio, input, content and launcher checks
remain. Motion-history tests now cover FSR bias and camera discontinuities.

## Official FSR 4.1.2 extraction and comparison

The source DLL is official AMD SDK 2.4.0's upscaler **4.1.2.4555**. Its Git blob
identity was checked against AMD's tagged tree and SHA-256 is pinned in the helper:
`aad4fccccb29084a655f9a4f9f613bd22c8c03fb4a0f8840148cc6543b4ccace`.
The model validator recognizes `v07_fp8_no_scale` and direct FidelityFX exports.

`tools/fetch_fsr412_assets.sh` ran end to end using the existing development tools:
checksum, 40 INT8/FP8 captures, SPIR-V translation and matching replay rules,
publication of the complete set and its `source.json`. Assets/source DLL remain
outside Git. The Linux game does not load the Windows DLL.

| FP8 synthetic comparison against the DLL | Result |
| --- | --- |
| 1706×960 → 2560×1440, 30 frames | Byte-identical |
| 1708×960 → 2560×1440, 30 frames | Byte-identical; covers the aligned runtime input observed in the scene |
| 1280×720 → 1920×1080, 3 frames | Byte-identical |
| 640×360 → 1920×1080, 3 frames | Byte-identical |
| 1067×600 → 1600×900, 3 frames | Byte-identical |
| 1707×960 → 2560×1440, 3 frames | Byte-identical |
| 1280×720 → 3840×2160, 3 frames | Byte-identical |
| 2259×1271 → 3840×2160, 3 frames | Differs: 223699 bytes |
| 1707×720 → 2560×1080, 3 frames | Differs: 223388 bytes |

The two differing cases are retained as limitations, not counted as passing a
universal bit-exact claim. These tests also do not certify image quality during
every motion/disocclusion/lighting condition in the game.

Three alternating old/new pairs of 600 synthetic frames at 1706×960 → 2560×1440
recorded six 300-frame GPU windows per asset set. Median old pass time: **0.8885 ms**;
SDK 2.4 assets: **0.8870 ms**. The difference is too small to establish a speedup;
both are approximately 0.89 ms on this workload. This is model cost, not game FPS.

For a target comparison, use the capture runner with native FP8 visible:

```sh
source tools/fsr4cap/proton.sh
export VKD3D_DISABLE_EXTENSIONS=
# Set PROTONPATH to the usable installed Proton and R to the capture work folder.
fsr4cap_run "$R" 4.1. 1706x960 2560x1440 30 noise
BB_FSR411_DIR="$HOME/.local/share/cachyborne/fsr4_412" \
BB_FSR411_VARIANT=fp8 BENCH_NOISE=1 BENCH_DUMP=/tmp/replay.raw \
  out/gpu/fsr4-bench 1706x960 2560x1440 1 30 --fsr411
cmp "$R/output_2560x1440.raw" /tmp/replay.raw
```

## Local installation and game smoke

The installed commands point at this checkout; the user installer refreshed both
menu/desktop entries and Sweet Purple icons. Current preferences and personal
defaults use FSR replay, the new asset directory, automatic/native FP8 selection,
1706×960 → 2560×1440, 144 FPS target, Mailbox and frame generation off. The generated
launch environment confirms P/E masks and **5056 MiB** direct memory. A private
600-permission copy of the previous settings/defaults/profile was kept under the
user's configuration backups. Existing bindings, mods, servers and original saves
were preserved.

Preparation successfully checked all **26913 DCX files**, recognized the 1.09
executable, linked the runtime/modules and published a new generation. Existing
selected mods and their separate asset view were retained.

- The offline title-menu smoke ran for **55 seconds**, remained alive and exited
  cleanly when requested. It is a UI-only scene and does not itself test FSR dispatch.
- A copied-save scene smoke then confirmed actual **FP8 t2160_m0** reconstruction
  and scene draws. After the final Pera changes, the repeat lasted **43.1 seconds**,
  including over 20 seconds after scene draws exceeded 600/frame, then exited 0
  on controlled termination. The log reported scene texture LOD bias **-1.59**
  and loaded the FP8 replay. Warm observation windows reported about **142–142.5 FPS**
  near the configured cap; loading/transition frames included larger spikes.
- SHA-256 checks of original saves before/after both copied-save scene tests were
  unchanged. Temporary test users were removed afterwards.
- Sampled process RSS peaks were roughly 0.88 GiB at the title and 1.09 GiB in the
  final copied-save run. RSS does not account for all imported/shared GPU memory,
  kernel caches or total system pressure, and is not an all-session RAM budget.
- Approximately **1952.5 MiB of apparent file size** in obsolete owned captures,
  temporary Proton prefixes, test-user copies and the superseded DLL were removed.
  Current assets/source/tools, fallback assets, private backups and game data remain.

No long traversal, repeated lamp-travel GC soak, complete boss/physics validation,
network-peer interoperability or exhaustive leak test was performed. A short scene
near the FPS cap cannot establish a before/after performance improvement or perfect
behavior in every scene. [Remaining work](ROADMAP.md) and
[selection rationale](COMMUNITY_AUDIT_0.8.2.md) describe those boundaries.
