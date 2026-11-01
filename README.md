# Cachyborne 0.8.2 — Midnight for CachyOS

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

[Release notes](docs/RELEASE_0.8.2.md) · [Validation](docs/VALIDATION_0.8.2.md) ·
[Community audit](docs/COMMUNITY_AUDIT_0.8.2.md)

## Target profile

| Component | Configuration |
| --- | --- |
| GPU | Radeon RX 9070 XT, RDNA4/GFX1201, Mesa RADV |
| CPU | i5-14600KF; P CPUs 0–11, E CPUs 12–19, verified on this PC |
| Memory | 16 GB system RAM; disk assets/caches; no full game in tmpfs |
| Image | FSR 4.1 Quality, 1706×960 scene → 2560×1440 output |
| Model | Capability-based automatic selection; native FP8 on this GPU |
| Presentation | Mailbox, 144 FPS target cap; frame generation off |
| Build | Clang, native CPU instructions, ThinLTO and mold |

The FPS target does not guarantee that rate in every scene. CPU numbering is
machine-specific. Foreground simulation/audio/render threads use P CPUs; named
shader/decompression workers use E CPUs, intersected with the allowed cpuset.

## Install

```sh
git submodule update --init --recursive
./cachyborne-install.sh
cachyborne-setup /path/to/app0 --performance-profile 144
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
cachyborne-setup /path/to/app0 --performance-profile 144
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
