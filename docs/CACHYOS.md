# Cachyborne on CachyOS — 16 GB RAM

Run `./cachyborne-install.sh` as your desktop user. It installs dependencies with
`sudo pacman -Syu --needed`, builds using Clang + mold + native CPU instructions
and ThinLTO, and registers commands in `~/.local/bin`, menu entries and separate game/configuration desktop shortcuts. Keep the checkout in place.
The binary is specific to the machine where it was compiled. Header dependencies
not packaged in the current repositories (magic_enum, VMA, Xbyak) are fetched by
CMake at pinned versions. Installation needs network access. Neither setup nor
launch installs packages or compiles the project. `./cachyborne-install.sh --user`
uses existing system/private-prefix dependencies without administrator access.

```bash
cachyborne-setup /path/to/decrypted/app0 --performance-profile 144
cachyborne-launch --dry-run
cachyborne-launch
```

Without installed commands use `./cachyborne-setup.sh` and
`./cachyborne-launch.sh`. `cachyborne-setup --gui` opens GTK setup.
`cachyborne-gui` has hardware, display/audio/controller and setup pages. Save
preferences, then run Setup to publish them. Launch always uses the last
successfully prepared profile, including when the GUI has unsaved changes.

Setup checks the executable identity using game_check.py and checks all DCX
files. It prepares the linked executable, patches and content profile in a new
private generation. Only a successful setup replaces the JSON profile. Setup
refuses `BB_SKIP_GAME_CHECK=1`. Every dump/version/mod/patch change requires setup
again. `--mods-dir DIR --mods-config FILE` uses the existing mods.json selection;
`--patches-dir DIR --patches-config FILE` compiles selected XML patches during
setup. Mod generations retain a symlink view of the source assets; keep both
the original app0 and the selected mod files in place. They are never modified.
FSR 4.1.1 selection checks the relevant replay assets; prepare these separately
with `tools/fsr4cap/build_assets.sh` using your own required inputs.

The Midnight GTK control center also exposes the complete original bbport editor
through Controles y extras. Apply those settings to Cachyborne, then run Setup;
all INI keys, bindings, mod/patch selections and launch variables are preserved.
See [the configuration catalog](CONFIGURATION.md). The recommended 144 FPS target
uses FSR 3.1 Quality at 1706x960 with 2560x1440 output; sustained FPS must be measured.

Setup publishes a symlink to a generation containing both the JSON profile and
a safely quoted Bash execution plan. Warm direct launch does not start Python.
Diagnostics, legacy regular JSON profiles and cold RAM staging use Python.

The JSON profile lives at `~/.config/cachyborne/game_profile.json`, preferences
at `settings.json` in the same directory. Generations and saves are under
`~/.local/share/cachyborne`, Mesa's persistent cache under `~/.cache/cachyborne`.
XDG_CONFIG_HOME, XDG_DATA_HOME and XDG_CACHE_HOME are respected.
Do not delete active generations. Old prepared generations can be removed
manually while the game is stopped, retaining the directory named in the profile.

## Memory and CPU settings

The default `16gb` profile keeps the roughly 30 GB game on disk and disables
full RAM staging. Direct memory is 5056 MiB for internal resolutions up to 1080p;
higher internal resolutions retain the 9152 MiB pool required by the resolution
patches. The port's extra pool and render resource cache are paired at 256 MiB
each. Two pipeline compiler workers
limit concurrent compiler memory, and MADV_WILLNEED is disabled through the launch
environment to avoid unnecessary sparse-pool prefetch. Huge-page advice remains
available. The 4G Mesa cache budget is disk storage, not reserved system RAM.
Close memory-heavy applications before starting the game on a 16 GB system.


`--ramdisk` enables rsync staging to the private tmpfs directory
`/dev/shm/cachyborne_ram_<uid>/<generation>`. Setup preloads it before publishing
the profile; a reboot requires another cold preload. `cachyborne-launch --stage`
warms it explicitly without launching. It reports size, progress and elapsed
time, checks tmpfs capacity and MemAvailable (8 GiB reserve), serializes copies
with flock and reuses only a completed generation. Source symlinks are followed
so a staged mod view contains actual files. Cold copying takes time; a warm
launch does not scan or copy the asset tree. Saves and shader caches persist on
disk. Kernel swapping can still evict tmpfs pages; zero I/O latency and complete
stutter elimination are not guaranteed. The host's /dev/shm size limits the copy;
the runner does not resize mounts or alter kernel policy.

P CPUs default to `0-11`, E CPUs to `12-19`. Check `lscpu -e` and edit the CPU
lists in preferences for your topology. Pinning applies to guest threads and
named host GPU workers, including asynchronous pipeline compilers. A constructor
captures the initial allowed mask so background workers can move to E CPUs even
when their parent was pinned. External taskset/cgroup limits remain respected.
Do not wrap the entire process in taskset 0-11 if you want to use E CPUs. Mesa's
internal workers are managed by Mesa; only port-owned worker roles are assigned.
`--no-pinning` disables host affinity. The guest's seven PS4 CPU bits remain
independent of Linux CPU numbers and do not configure realtime priorities.

MADV_HUGEPAGE and MADV_WILLNEED are best-effort hints on backing views, committed
chunks/aliases and low anonymous allocations. Unsupported dma-buf advice is
ignored. Reservations are not prefaulted. Set `BB_HUGEPAGES=0` or
`BB_MEMORY_WILLNEED=0` for comparisons. THP depends on kernel configuration,
alignment, shmem policy and page tracking. Read `/proc/PID/smaps` to inspect
actual huge-page coverage. No privileged sysfs writes are performed.

## GPU and scheduler settings

The RADV preset selects the standard Radeon ICD when installed, enables SAM's
Mesa setting, requests 16x anisotropy, disables implicit layers and reserves a
4G persistent Mesa shader cache budget. Explicit environment overrides remain
available. The requested `RADV_PERFTEST=dcc_msaa` spelling is invalid; documented
`dccmsaa` concerns older GPU compression paths and is not forced on GFX12.
Modern RADV already uses ACO. ReBAR availability is controlled by firmware and
the driver; the existing Vulkan memory capability checks remain active. PCIe
access and shader compilation still have latency. Mailbox is the default request;
the existing renderer handles device-supported presentation modes.

GameMode and Gamescope are optional. Gamescope exposes output size, fullscreen,
HDR, spatial FSR and Adaptive Sync. Their effectiveness depends on the display,
compositor and installed Gamescope. Fullscreen on Wayland follows the compositor.
Scheduler selection either accepts the current system scheduler or requires an
already active scx_lavd/scx_bpfland. Enable it through CachyOS's scheduler manager
before launch. The runner does not replace the system scheduler or start privileged
services. Existing ananicy-cpp rules can identify the executable as `bb-probe`.

The default direct path is offline and starts no community server or authentication
helper. Online accounts and options are available in the complete editor. When
enabled, setup prepares routing files and selects the online module; server
connections occur when the game runs. Local server fallback processes are not
started automatically.

## Desktop and verification

The GUI's Crear Acceso Directo action (also
`python3 launcher/cachyborne.py desktop --desktop --download-icon`) creates separate game and configuration entries in both the menu and desktop.
The game entry directly runs `cachyborne-launch`. Installer entries provide separate settings and play
commands. Add the direct launch command as a non-Steam game if desired.

```bash
bash scripts/check-all.sh
clang -std=c11 -O3 -Wall -Wextra -Werror -pthread -I. \
    tests/test_cachy_affinity.c -o out/cachy-affinity-test
out/cachy-affinity-test
```

Measure profile-to-exec overhead separately from cold RAM staging, optional
wrappers and game initialization. The <50 ms objective applies to a prepared,
warm direct path; it is not a hardware-independent guarantee. Gameplay frame
times, Central Yharnam traversal, HDR/VRR and total load time need a real game
session. See RESEARCH_AND_ARCHITECTURE.md for findings and validation results.
