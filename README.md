# Cachyborne 0.8.6 — Midnight for CachyOS

**English** · [Español](README.es.md)

Native Bloodborne on Linux x86-64, based on stable bbport 0.7. Original game code
runs on the host CPU; the C runtime supplies PS4 services and the C++ renderer
translates GPU commands and shaders to Vulkan. Midnight uses GTK4/Libadwaita.
You need your decrypted **1.09** dump with the update/DLC merged. This repository
distributes no game, saves or proprietary AMD binaries.

This release reviews community ports, extends guest-memory ownership checks to
GPU copies/clears, handles null SRT subtrees without per-draw signals, sanitizes
motion vectors, prepares **AMD FSR 4.1.2** assets and introduces separate original
Sweet Purple game/settings icons. Port-owned menus support English and Spanish;
speech is selected independently.

The [2026-11-01 convergence audit](docs/CONVERGENCE_AUDIT_2026-11-01.md) adds
bounded parallel DCX validation, library-worker affinity reconciliation,
time-bounded host staging, immediate Vulkan timeline completion publication,
and complete Spanish coverage of the current launcher catalog and setup errors.
Validation includes 26 C++ tests, 239 Python tests and native FP8 replay on the
target RX 9070 XT. The report records remaining physics/travel acceptance work.

The [consolidated follow-up audit](docs/CONVERGENCE_AUDIT_FINAL.md) documents a
native libdeflate hook with original-guest parity tests, optional contact shadows
and PCSS, a software latency HUD (`BB_LATENCY_HUD=1`), and copied-save trajectory
and repeated-lamp-travel tools. Midnight exposes Contact shadows and Soft
directional shadows (PCSS).
`BB_CONTACT_SHADOWS=1` and `BB_PCSS=1` override those settings; both default off.
Software present latency does not measure physical scanout. Full Havok equivalence, POM and Lantern
room interoperability remain uncertified; the report records the outstanding work.

Version 0.8.3 restores all resident linear mips without overwriting guest padding,
uses compact staging and per-slice memory transactions, and adds particle-time
and null online-event guards. [Source review](docs/COMMUNITY_AUDIT_0.8.3.md).

[Release notes](docs/RELEASE_0.8.6.md) · [Validation](docs/VALIDATION_0.8.3.md) ·
[Community audit](docs/COMMUNITY_AUDIT_0.8.3.md)

## Target profile

| Component | Configuration |
| --- | --- |
| GPU | Radeon RX 9070 XT, RDNA4/GFX1201, Mesa RADV |
| CPU | i5-14600KF; P CPUs 0–11, E CPUs 12–19, verified on this PC |
| Memory | 16 GB system RAM; disk assets/caches; no full game in tmpfs |
| Image | FSR 4.1 Native AA (1×), 2560×1440 scene and output |
| Quality | Full textures, GT-VBAO (SSAO enabled), dynamic shadows and bloom; RCAS 0.35 |
| Model | Capability-based automatic selection; native FP8 on this GPU |
| Presentation | Mailbox, 60 FPS target cap; frame generation off |
| Build | Clang, native CPU instructions, ThinLTO and mold |

The default cap is 60 FPS (16.667 ms); frame-time consistency must be measured in game.
SMAA is selected in settings and runs when the upscaler is off; with FSR, temporal AA applies.
Chromatic aberration, motion blur and DoF are off. The default `preset=0` means
**Native AA (1×)**; `preset=1` selects Quality (1.5×). Resolution-sized graphics
heaps use a 5936 MiB guest pool at native 1440p; CPU-facing guest memory stays
cached while GPU resources and mirrors use VRAM. The local GTT budget is checked
before preparing or launching this profile.
Audio uses a monotonic clock, a prefilled SDL queue and P-core placement;
`BB_AUDIO_STATS=1` exposes underruns for hardware verification.

The FPS target does not guarantee that rate in every scene. CPU numbering is
machine-specific. Foreground simulation/audio/render threads use P CPUs; named
shader/decompression workers use E CPUs, intersected with the allowed cpuset.

The fixed-60 profile uses stable flip pacing, an adaptive sleep margin and
parked idle workers. The [ongoing BBHost audit](docs/BBHOST_0.2.24_AUDIT.md)
records three 120-second native FSR 4.1 FP8 camera sweeps at Mensis, Old Yharnam
and Forbidden Woods, each with 7,200 frames and 16.71 ms p99. A Cathedral ABBA
comparison used 12.5% less CPU time with this policy. These are specific scenes
and builds. An earlier build also passed a 600-second physical
1440p Mensis camera sweep with USB audio: 36,000 frames, 59.993 FPS, p99
16.72 ms, worst 16.95 ms.

The November 6 build passed a 120-second physical Mensis sweep with USB audio:
7,200 frames, 59.995 FPS, p99 16.71 ms, worst 16.98 ms. A separate 120-second
headless capture with short character movements in Hunter's Dream and host
telemetry measured 59.992 FPS, p99 16.71 ms, worst 16.86 ms. This build adds native
parameter-ID enumeration and cache ownership/publication fixes; 123 retail C++
tests pass without skips, alongside 333 Python tests. The
[follow-up](docs/BBHOST_FOLLOWUP_2026-11-06.json) retains hashes and limitations.
The [native plugin prefix](docs/BBHOST_PLUGIN_HOST_V1_AUDIT.json) implements all
12 version-1 services and passes real DSO/native HLE, ASan/UBSan and targeted
TSan tests. A test adapter verified its phases in the actual game; normal-launch
activation and the remaining API16 services are still pending.
Broader gameplay, complete BBHost integration and the final release remain in progress.

## Install

```sh
git submodule update --init --recursive
./cachyborne-install.sh
cachyborne-setup /path/to/app0 --performance-profile 60
cachyborne-launch --dry-run
cachyborne-launch
```

The installer uses pacman with sudo, builds and registers user commands/menu and
desktop entries. `--user` uses existing dependencies; `--user --skip-build` installs
an existing build. Keep this checkout and `out/` in place. Direct equivalents are
`cachyborne-setup.sh`, `cachyborne-launch.sh` and `cachyborne-gui`.

Setup validates executable identity/DCX files, prepares linked modules/patches and
a separate mod view, then atomically publishes a generation. Failed preparation
preserves the old profile. Normal launch consumes prepared files without package
installation, compilation or a full content scan. Prepare again after changing
settings, mods, patches or game content. The source dump remains untouched.

## Current FSR assets

```sh
# Optional extraction needs MinGW/SPIR-V tools and a usable Proton installation.
bash tools/fetch_fsr412_assets.sh
cachyborne-setup /path/to/app0 --performance-profile 60
```

The helper downloads official **AMD SDK 2.4.0 / upscaler 4.1.2.4555**, verifies a
pinned SHA-256 and builds native Vulkan replay assets in
`$XDG_DATA_HOME/cachyborne/fsr4_412` (default `~/.local/share`). The DLL is used only
for extraction, not loaded by the Linux game. The internal `fsr411` setting key
is retained. Missing/invalid assets produce an explicit FSR3 fallback.

The three-frame synthetic comparison is byte-identical at the target 1440p size;
two tested 4K/ultrawide combinations differ and remain experimental. Updating the
SDK does not establish a game-FPS improvement or universal bit equivalence.
[Details and other backends](docs/upscaler.md).

## Configuration and updates

`cachyborne-gui` provides native graphics/performance, controls/bindings, mods,
patches, online/account, trophy and diagnostic pages. Save and prepare to publish
changes. The recommended profile preserves content selections, bindings, HDR and
servers. Text defaults to Spanish and speech to English; menu language remains
independent. Missing voice alternatives preserve the game's original audio.

The existing BBSC/shadNet co-op implementation does not implement Lantern's room
protocol; those features cannot be enabled with a launcher switch alone.

```sh
git pull --ff-only
git submodule update --init --recursive
bash scripts/check-all.sh
./cachyborne-install.sh --user --skip-build
cachyborne-setup /path/to/app0
```

The check script builds runtime/GPU tests, runs CTest/Python, validates SPIR-V and
exercises real Vulkan. `build.sh` applies versioned patches to the pinned FSR
submodule; proprietary assets and generated caches stay outside Git. Debug fault
waiting is optional in Midnight; `tools/debug_attach.sh PID` saves a private GDB
report and detaches without killing the process by default.

[Configuration](docs/CONFIGURATION.md) · [CachyOS/memory](docs/CACHYOS.md) ·
[Upscaling](docs/upscaler.md) · [Mods](docs/MODS.md) · [Co-op](docs/BBSC.md) ·
[Environment](docs/ENVIRONMENT.md) · [Architecture](RESEARCH_AND_ARCHITECTURE.md) ·
[Remaining validation](docs/ROADMAP.md)

## License

The Cachyborne build includes BBHOST adaptations under GPL-3.0-or-later;
see [NOTICE](NOTICE), the [GPLv3 text](LICENSES/GPL-3.0-or-later.txt), and the
[source attribution inventory](docs/BBHOST_LICENSE_AUDIT.json). Original
bbport code keeps its GPL-2.0-or-later grant in [LICENSE](LICENSE).
Third-party components retain their own licenses.
