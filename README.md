# Cachyborne 0.7.0 — Bisexual Midnight for CachyOS

Linux-native Bloodborne runtime specialized for **CachyOS, 16 GB of system RAM,
Intel Core i5-14600KF and AMD Radeon RX 9070 XT with Mesa RADV**. Based on
[bbport / deadinside28/bloodborne_pc](https://github.com/deadinside28/bloodborne_pc).
The game's original x86-64 executable runs on the host CPU; the runtime replaces
PS4 system interfaces and translates graphics to Vulkan.

You must supply your own decrypted retail Bloodborne dump with update **1.09**
merged into the game directory. Game files, saves and proprietary upscaler assets
are not included or downloaded by this project.

## Install

From a checkout of this repository:

```bash
git submodule update --init --recursive
./cachyborne-install.sh
```

The installer uses `sudo pacman -Syu --needed`, builds with Clang/LLVM, mold,
`-march=native -mtune=native -O3`, and ThinLTO, then registers commands in
`~/.local/bin`, menu entries and game/configuration shortcuts on your XDG desktop.
The resulting binary targets the CPU on which it was built. Keep the checkout
and its `out/` directory in place.

For a user installation with existing dependencies, including development
packages already extracted under `out/native-deps/usr`, use:

```bash
./cachyborne-install.sh --user
```

`--user` does not run pacman or request administrator access. It still builds
and checks for the runtime artifacts. `--skip-build` registers an already built
checkout. Missing header-only dependencies are fetched by CMake at pinned
versions during the build; the first build needs network access.

## Prepare once, launch directly

```bash
cachyborne-setup /path/to/CUSA03173 --performance-profile 144
cachyborne-launch
```

Without installed commands, use `./cachyborne-setup.sh` and
`./cachyborne-launch.sh`. The game directory must contain `eboot.bin`,
`sce_module`, `sce_sys` and the extracted assets.

Setup validates the executable identity and compressed assets, links the game
modules, compiles patches and prepares persistent saves/caches. It publishes a
profile only after success. Run setup again after changing the dump, mods,
patches or prepared settings. A failed setup preserves the previous profile.

Launch uses the prepared Bash execution plan and directly executes `bb-probe`.
It does not build, hash the game or recompile patches. `run.sh` is an alias for
this prepared launch path. A clean installation must complete setup first.

```bash
cachyborne-launch --dry-run             # inspect command and environment
cachyborne-gui                          # GTK4 / Libadwaita settings
cachyborne-setup --gui                  # open the setup interface
python3 launcher/cachyborne.py desktop --desktop --download-icon
```

The GUI has separate setup, play and shortcut actions. Save preferences and run
Setup to publish them; Play uses the last successfully prepared profile. The two desktop entries open the game and settings separately. The game
shortcut launches directly without opening the settings hub.
It can also be added to Steam as a non-Steam application.

## The Midnight control center

The GTK4/Libadwaita interface matches the BiMidnight palette: near-black violet
surfaces, lavender text and pink, purple and blue accents. A sidebar separates
home, performance, graphics/audio, controls and setup. Native high-contrast mode
keeps its accessible appearance. GTK 4.16+ and Libadwaita 1.6+ are required;
current CachyOS packages provide them.

**Controles y extras → Abrir configuración completa** opens the complete original
bbport GTK editor with the same theme. All original settings remain available:
controller/keyboard remapping and mouse options, game/UI language, display/GPU,
aspect ratio, effects/LOD, FSR/DLSS asset tools, custom FPS limits, mod ordering,
XML patches, online accounts and diagnostics. Choose **Aplicar a Cachyborne**,
then **Validar y preparar** in the main window. The full INI and environment are
carried into setup and direct launch; the complete editor does not start a second
launch workflow. See the [configuration catalog](docs/CONFIGURATION.md).

The recommended profile targets **144 FPS**, with 1440p output and FSR 3.1 Quality
at 1706×960 internally. Motion blur, chromatic aberration and depth of field are
disabled. The native FPS counter is enabled. This is a target, not a guarantee
of sustained frame rate in every area. `--performance-profile 60` provides a
lower target; all individual options remain editable.

Setup shows file-validation progress and supports cancellation. A failed or
cancelled preparation retains the previous profile. Game-side INI changes newer
than the GUI draft are kept when loading settings, including custom bindings.

## The 16 GB memory profile

- The roughly **30 GB game stays on disk**. Full tmpfs staging is disabled by
  default and cannot fit this machine's available RAM or `/dev/shm` capacity.
- Direct memory uses **5056 MiB** at internal resolutions up to 1080p. Higher
  internal resolutions retain the 9152 MiB pool needed by the resolution patches;
  they increase memory pressure on a 16 GB system.
- The port pairs a 256 MiB extra direct pool with a 256 MiB render resource cache
  and uses two asynchronous pipeline compiler workers. Pool prefetch is disabled by default through
  `BB_MEMORY_WILLNEED=0`; transparent huge-page advice remains enabled.
- The persistent Mesa shader cache has a **4G disk budget**. This is disk storage,
  not a reservation of system RAM. Saves and caches remain persistent.

Close memory-heavy applications before playing. Optional RAM staging remains
available for smaller asset trees or machines with sufficient free memory; it
checks both tmpfs capacity and available RAM, retaining an 8 GiB reserve.

The installer downloads Bloodborne artwork from the official
[PlayStation page](https://www.playstation.com/en-us/games/bloodborne/) for your
local game icon. The artwork remains in your user icon directory and is not
bundled in the repository. `--download-icon` refreshes it explicitly; an offline
failure preserves the previous icon and does not prevent shortcut installation.
The GUI's shortcut action creates both game and configuration entries on the
desktop and in the application menu.

## CPU, GPU and display

The default CPU topology is P CPUs `0-11` and E CPUs `12-19`. Foreground guest,
audio and named rendering workers use P CPUs; pipeline compiler/cache workers
can use E CPUs. The policy respects the process's initial allowed mask. Verify
CPU numbering with `lscpu -e`; change the lists in preferences when needed.

The RADV preset selects an installed Radeon ICD, uses driver-managed SAM/ReBAR,
requests 16x anisotropy and disables implicit Vulkan layers. Explicit environment
overrides are supported. Vulkan memory capability checks remain active; ReBAR
and huge pages do not eliminate physical memory or PCIe latency.

GameMode and Gamescope are optional. Gamescope settings include output size,
fullscreen, HDR, spatial FSR and Adaptive Sync. These require suitable display
and compositor support. Mailbox is the default presentation request. FSR 3.1 is
available; FSR 4.1.1 requires separately prepared assets and compatible GPU
features. See [the CachyOS guide](docs/CACHYOS.md) for details.

Scheduler settings use the current scheduler or require an already active
`scx_lavd`/`scx_bpfland`. Enable the selected scheduler through CachyOS's scheduler
manager. The launcher does not start privileged services or change kernel policy.
The prepared direct path is offline; it starts no community server or online
authentication helper. Online modules can still be built and regression-tested.

## Configuration and saves

| Data | Default location |
| --- | --- |
| Validated profile | `~/.config/cachyborne/game_profile.json` |
| Draft GUI preferences | `~/.config/cachyborne/settings.json` |
| Prepared generations | `~/.local/share/cachyborne/prepared/` |
| Saves and renderer caches | `~/.local/share/cachyborne/user/` |
| Mesa cache | `~/.cache/cachyborne/` |

XDG directory overrides are respected. The profile symlink atomically identifies
a generation containing both JSON and its quoted shell execution plan. Keep the
source dump and selected mod files in place. Neither setup nor launch modifies
the supplied game assets. Do not remove active prepared generations.

## Build and test

```bash
bash build.sh                          # native runtime and GPU modules
bash scripts/check-all.sh              # full project regression suite
```

The full check runs shell/Python checks, native C regressions, all registered
C++/Vulkan CTest targets, Python tests, SPIR-V validation when available, and the
Vulkan smoke test. Device-backed tests require a working Vulkan driver. Shader
corpus replay additionally requires captured `.bbsc` records; the regular suite
runs its independent auxiliary hash regression. No game dump is required for
unit tests. Real-game validation is a separate setup and launch step.

See [research and architecture](RESEARCH_AND_ARCHITECTURE.md) and the
[CachyOS guide](docs/CACHYOS.md) for implementation details and measured limits.
Launcher overhead measurements exclude game initialization and cold asset copies;
they do not establish gameplay FPS or prove stutter-free traversal.

## Releases

[Version 0.7.0 notes](docs/RELEASE_0.7.0.md) and the
[validation record](docs/VALIDATION_0.7.0.md) describe the source distribution and
validation scope. Source release assets include the pinned submodules and a
SHA256SUMS file. Extract the source archive and run `./cachyborne-install.sh`;
the installer builds for the local CPU. Game files, saves and account data are
never included. `python3 scripts/package-release.py` packages tracked sources
and submodules only.

## Credits and license

This fork builds on bbport, its native runtime and the Vulkan renderer derived
from shadPS4, plus the third-party projects identified in their source directories
and `gpu/VENDOR.txt`. Preserve their licenses and attribution. See [LICENSE](LICENSE).
