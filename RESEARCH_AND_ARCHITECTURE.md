# CACHYBORNE: Research and architecture

Research date: 2026-10-25. Target: CachyOS, i5-14600KF, RX 9070 XT,
16 GB RAM. This document records the investigation before implementation.

## Existing execution path

`run.sh` merges mods, calls prepare.py, link_libc.py, link_modules.py and
content_profile.py, determines resolution, compiles patches, probes the GPU,
sets runtime defaults, builds, optionally starts network services, then execs
bb-probe. Its final command requires boot-linked.bin, content.bin, patches.bin,
--app0, --user and --timeout. Copying only the eboot and patches is insufficient.
`launcher/bbport_launcher.py` already uses GTK4/Libadwaita, but combines settings,
asset tooling, online authentication, setup and launching. A dedicated preferences
entry point will provide the CachyOS workflow without importing that monolith.

`scripts/game_check.py` provides executable identity checks through problem() and
image_sha256(), but its CLI only decompresses DCX files. Setup must invoke both.
prepare.py also validates the executable. Setup must refuse BB_SKIP_GAME_CHECK.
Prepare/link outputs must be generated in a fresh directory, then published with
an atomic profile replacement; a failed setup must leave the old profile usable.
A new setup generation is required after changes to game assets, mods or patches.

## Memory investigation

runtime_memory.c owns a sparse memfd of roughly 5.5 GiB plus a 1 GiB flexible
span, not 16 GB. MAP_SHARED aliases preserve physical identity. A second backing
view bypasses guest page protection. Direct memory can be replaced with 256 MiB
GPU dma-buf chunks or whole-allocation regions; imported host memory also exists.
map_phys creates guest aliases, splitting at chunk boundaries. PROT_NONE ranges
are reservations, not committed pools. runtime_low_map allocates anonymous low
address stacks and records.

Apply MADV_HUGEPAGE to the successful memfd backing mapping and each successful
committed alias/chunk mapping. MADV_WILLNEED is a best-effort prefetch hint on
these mappings, not a promise of populated anonymous pages. Preserve errno and
ignore unsupported advice on dma-buf mappings. Never advise failed mmap results
or PROT_NONE reservations. Expose BB_HUGEPAGES=0 and BB_MEMORY_WILLNEED=0 for
measurement. Do not mlock or eagerly touch 16 GB. GPU page protection and small
alias alignment can split huge pages; shmem THP policy matters independently of
anonymous THP. No claim of zero TLB misses is physically supportable.

## CPU investigation

runtime_thread.c records guest priority and 7-bit PS4 affinity but does not set
Linux scheduling affinity. Guest logical CPU numbers cannot be treated as Linux
CPU IDs. Apply a separate host policy at thread startup/host attachment: foreground
main, simulation, audio and rendering on configured P CPUs (default 0-11),
explicit background asset/decompression work on E CPUs (12-19). Host GPU workers
need the same policy at their thread naming hook, including shader compilers.
Capture the process's original allowed mask before pinning; intersect requested
masks with it. Never widen a cpuset/container restriction, never fail game startup
on affinity failure, and allow disabling the policy. CPU numbering is a target
assumption, not a universal Intel ABI. Keep PS4 affinity reporting intact.

A process-wide taskset 0-11 would prevent inherited background threads from using
E CPUs unless the original allowed mask were retained separately. Prefer per-thread
pinning at thread entry to a launcher-wide taskset. Do not infer realtime Linux
priorities from PS4 priorities or request CAP_SYS_NICE. scx_lavd/bpfland operate
system-wide; check the selected active scheduler without replacing a running
scheduler or launching privileged services. ananicy can match bb-probe by name;
GameMode remains optional and independently selectable.

## Native toolchain

build.sh currently defaults to cc, runtime -O2 and generic GPU code. gpu/CMakeLists
uses IPO if available, with a generic CPU deliberately retained for Steam Deck.
Require Linux, Clang/Clang++, mold; use -march=native -mtune=native -O3 -pipe
-fno-plt -fno-semantic-interposition and ThinLTO (BB_LTO=ON). Runtime and GPU must
use matching toolchain flags; retain frame pointers and avoid -ffast-math. Clang
profile-use requires an LLVM profdata workflow; reject the existing GCC .gcda
PGO path in the native build rather than silently applying incompatible flags.
Remove Windows platform selection from the native CMake path. Vendored platform
sources remain outside the target rather than rewriting third-party dependencies.

## RADV, ReBAR and presentation

Mesa's current environment documentation lists `sam`, `nosam`, and `dccmsaa`;
`dcc_msaa` is not the documented spelling. ACO is RADV's normal compiler; no
extra compiler switch is needed. Set AMD_VULKAN_ICD=RADV and select the system
RADV manifest when present; keep explicit VK_DRIVER_FILES overrides. Use a
persistent 4G Mesa cache. Allow RADV_TEX_ANISO=16 and disable implicit layers
only under the selected RADV preset; both are user-overridable. Do not force
obsolete/unsupported flags for GFX12: SAM/ReBAR memory availability is detected
by the driver and firmware. Existing guest GPU memory/import paths already query
usable heaps and extensions. Host-visible VRAM is finite; forcing every buffer
there can reduce performance. ReBAR still crosses PCIe and cannot have zero
latency. Do not remove allocation fallbacks needed for actual device capabilities.

Mailbox is tear-free and can reduce queued-frame latency when supported; immediate
can tear; FIFO is the guaranteed portable presentation mode. Use the renderer's
existing present-mode setting and gamescope's optional VRR/HDR controls. No claim
of exclusive fullscreen on a Wayland compositor. FSR 4.1.1 requires externally
prepared assets already supported by this repository; a GUI selector cannot
create missing proprietary assets. Reject unavailable FSR 4.1.1 at setup.

Sources: https://docs.mesa3d.org/envvars.html (RADV, cache configuration);
https://docs.kernel.org/admin-guide/mm/transhuge.html (anonymous/shmem policies);
https://github.com/sched-ext/scx/tree/main/scheds/rust/scx_lavd (scheduler scope).

## Three-tier design

1. cachyborne-setup: CLI plus GTK entry, full validation, prepare/link/content/
patch compilation, persistent save/cache directories, atomic JSON profile at
~/.config/cachyborne/game_profile.json. Each generation has private artifacts.
2. cachyborne-launch: read profile, constant-size artifact checks, configure
environment/wrappers, exec bb-probe directly. Never hash assets, invoke build.sh,
compile patches or start network servers. A --dry-run reports the exact command.
3. cachyborne-gui: AdwPreferencesWindow, separate setup/launch/desktop actions,
hardware, display, audio/controls and setup preferences. Settings requiring
patch regeneration take effect only after successful setup; launch uses the
last prepared generation. Desktop launch invokes cachyborne-launch directly.

## RAM staging

Use /dev/shm/cachyborne_ram/<uid>/<generation> on tmpfs. Validate directory
ownership and reject symlink destinations, acquire flock, check available tmpfs
space and MemAvailable with a reserve, and rsync without modifying app0. Publish
a completion marker only on success. A warm launch checks that marker only;
a reboot or new setup generation stages again. Setup can warm the staging cache;
--stage provides an explicit preload action. Report bytes/time during staging.
Do not write saves or persistent shader caches into tmpfs. No root mount or
btrfs reflink is needed; reflinks do not copy assets into RAM. Cold copying and
optional wrappers cannot satisfy a universal <50ms startup guarantee. Measure
profile-to-exec overhead separately from RAM staging and game initialization.
Asset decompression, shader creation and synchronization may still cause stutter.

## Verification plan

Syntax/compile checks for shell, Python and modified C/C++; focused tests for
profile publication, launch command construction, stale/missing artifacts, RAM
staging failure and affinity mask selection. Attempt build.sh with the native
toolchain and record missing dependencies or build errors accurately. Run existing
Python tests. Runtime FPS/frame pacing, HDR/VRR, THP coverage, shader compilation,
RAM preload and startup latency require the game dump and target graphics session;
report them as unmeasured until exercised. Never report clean compilation or
stutter elimination based on script syntax checks alone.

## Implementation and measured verification

The launch implementation was refined after measurement: starting Python on the
hot path took 131.22 ms median in this session. Setup now writes both JSON and a
quoted Bash execution plan into one generation, then atomically publishes the
`game_profile.json` symlink. The shell entry reads this symlink and sources the
matching plan, checks only a fixed number of artifacts, sets environment and
execs the game. Python remains the setup, diagnostic and cold-staging backend.
A regular legacy JSON profile still uses that backend. The prepared shell path
measured **2.44 ms median, 2.16 ms minimum, 3.33 ms maximum over 20 samples**
using /usr/bin/true as the executable. These are launcher overhead measurements,
not real Bloodborne startup measurements; optional wrappers and cold RAM copies
were disabled. Raw timing output: out/launch-timing.json.

RAM directories use `/dev/shm/cachyborne_ram_<uid>/<generation>` to avoid
shared-directory ownership collisions. Mod views are prepared once and persist
with the generation; rsync follows their symlinks to materialize actual files
in tmpfs. A private completion marker bypasses directory scans on warm launches.
The GUI saves draft preferences separately; setup publishes the settings snapshot.
The new direct runtime path is offline and starts no network service.

The inspected build host exposes 15 GiB usable RAM and a 7.7 GiB /dev/shm mount,
consistent with the 16 GB installed-memory profile. No mount or kernel
policy was changed. Some native build dependencies were missing. Since sudo
requires an interactive password, build validation used CachyOS/Arch packages
extracted under ignored out/native-deps, plus pinned CMake header downloads.
The repository's FSR submodule patches were applied by build.sh as designed.
Native Clang also identified an unused write-only service flag; it was removed
without changing service behavior. Existing standalone pad/content tests needed
missing renderer/file-service stubs before their native targets could link.

GTK4/Libadwaita PreferencesWindow construction was exercised in the available
graphics session. The C affinity test verifies CPU-list parsing, real foreground
pinning, background movement outside the parent's narrowed mask, and disabling
policy. Python tests cover atomic publication/failure preservation, shell launch
without Python on PATH, argument/path quoting, wrappers, fixed prepared settings,
missing artifacts, insufficient RAM, interrupted rsync and warm staging reuse.
The supplied CUSA03173 dump was validated with zero failures among 26,913 DCX
files. A 45-second real-game smoke launch rendered frames on the RX 9070 XT
(RADV GFX1201) and returned zero after controlled termination. This verifies
startup and rendering, not a complete playthrough or traversal benchmark.
HDR/VRR, long-session stability and Central Yharnam traversal remain unmeasured.

Final checks completed: `build.sh --test` produced out/bb-probe and
out/gpu/libbbgpu.so with native Clang/ThinLTO/mold flags and passed the pad,
runtime memory/thread/service, file-mod, semaphore, content and affinity targets.
Both online modules were subsequently built after adding the local JSON header
package; the native online imports regression passed. The direct launcher uses
the offline path and starts no network helper. The build emits existing
upstream/vendor warnings, so this is a successful build, not a warning-free one.
`python3 -m unittest discover -s tests -p 'test_*.py'` ran **142 tests, all OK**
after the native test executables were available. Shell syntax checks, Python
byte-compilation, modified runtime C syntax checks and git diff --check passed.

Reproduce the local-dependency build from this session (or run the installer to
install these dependencies normally):

```bash
env PATH="$PWD/out/native-deps/usr/bin:$PATH" \
    CMAKE_PREFIX_PATH="$PWD/out/native-deps/usr" \
    LD_LIBRARY_PATH="$PWD/out/native-deps/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    LIBRARY_PATH="$PWD/out/native-deps/usr/lib" \
    CPATH="$PWD/out/native-deps/usr/include" bash build.sh --test
```


## Installed 16 GB configuration and full regression run

The active profile uses `memory_profile=16gb`, a 1920x1080 internal render size,
2560x1440 output, FSR 3.1 and a 60 FPS cap. Full RAM staging is off: the local
game is about 30 GB, larger than this machine's memory. The direct pool is
5056 MiB at this internal resolution, with 512 MiB extra for the render resource
cache, two pipeline compiler workers and a 4G persistent disk shader cache.
MADV_WILLNEED is disabled in the profile; THP advice remains active. Higher
internal resolutions retain the 9152 MiB pool required by their patches.

A user installation registered ~/.local/bin/cachyborne-{setup,launch,gui},
application menu entries and an executable direct shortcut in the XDG desktop
directory. It used the existing local dependency prefix and needed no sudo.
The supplied dump remains in place; game assets were neither downloaded nor
included in the repository. The profile and generated runtime files remain
outside the source checkout.

`bash scripts/check-all.sh` completed successfully: native C tests (including
online imports), **14/14 C++/Vulkan CTest targets**, **142 Python tests**, generated
motion shader SPIR-V validation and a Vulkan submission/readback smoke test on
the RX 9070 XT. Standalone GPU tests needed three additional process-service
stubs; required guest memory operations abort rather than silently succeeding
when no guest exists. Release test builds retain assertions. The real launch
also exposed a presentation-mode case mismatch; the launcher now emits the
renderer’s canonical `Mailbox`, `Fifo`, and `Immediate` names.

The README documents the actual supported installer, setup and direct-launch
commands. Its local documentation links, desktop entry syntax, installed GUI
profile, shell syntax and repository whitespace checks were verified. Runtime
logs and raw test results are kept in ignored out/ files.


## Follow-up: desktop integration and memory pressure

The user confirmed the game runs. The two desktop/menu entries now come from one
implementation, including the GUI's shortcut button. The game uses Bloodborne
artwork downloaded from Sony's image CDN linked by the official PlayStation page;
the configuration entry opens cachyborne-gui with a settings icon. Downloaded
artwork lives only under the user's XDG icon directory. The CDN returns JPEG for
the .png URL, so GdkPixbuf decodes it and writes a 512x512 PNG. A failed download
preserves the previous image and still installs the shortcuts.

For 16 GB, the extra direct pool and render resource cache now both default to
256 MiB, saving 256 MiB of potential backing compared with the original 512 MiB
extension while retaining a larger cache than the game's native 160 MiB. The two
values must change together: reducing only the pool would force the cache back
to its native size. The 5056 MiB base pool, two pipeline workers, disk shader cache
and disabled full RAM preload remain in place. This configuration is published
for the next launch, without changing a running game's memory allocations.

Additional fixes reject reversed/out-of-range host CPU lists, handle empty setup
log lines in the GUI, escape both layers of desktop Exec syntax, and link a new
bb-probe inode before replacing the executable. The latter avoids ETXTBSY when
an existing process is running. Tests exercise shortcuts, preserved icons on
network/error responses, JPEG-to-PNG decoding and the GUI callbacks.


Follow-up verification: 150 Python tests pass, alongside the existing 14 CTest
GPU/C++ targets, native C/online regressions, SPIR-V checks and Vulkan readback.
An independently started CPU-only synthetic bb-probe stayed alive throughout the
full rebuild; its executable inode was replaced without interrupting the process.
The installed GTK window and empty-log callback were also exercised. Both desktop
files pass desktop-file-validate. The active profile was refreshed into a new
prepared generation while preserving the existing INI and validated game artifacts;
the reduced extra pool/cache defaults will apply on the next launch. The user's
running game was not terminated by these changes.

## 0.7.0: Midnight control center and original configuration parity

The preferences grid was replaced by a GTK4/Libadwaita application window with a
sidebar, a prepared-profile dashboard, hardware/display/control pages and a
progress-aware setup workflow. Colors match the active BiMidnight desktop:
#140e1a, #1b1526 and #241e34 surfaces; #f7eafb text; #ea59a3, #ab6ef6 and #608cf6
accents. Styling is scoped to Cachyborne windows, retains the user's font and
leaves native high-contrast rendering available. No desktop theme files are edited.

The complete original GTK editor is integrated as a themed child window rather
than dropping its controls. Its 41 settings defaults, 24 INI defaults and 44
keyboard/gamepad assignments remain accessible. An adapter imports all settings,
full INI data, user/save directories, mod order, patch switches and environment
into the prepared profile. Game-side INI changes newer than a saved GUI draft
remain visible. Setup now passes that environment to patch compilation, handles
fixed/live resolution consistently and prepares online routing without network
probes. Advanced dry-run output redacts credentials; enabled diagnostic logging
and MangoHud work on the direct launch path. The renderer handles FIFO Relaxed.

The recommended default is a 144 FPS target, FSR 3.1 Quality, 1706x960 internal
render size and 2560x1440 output. It retains the 16 GB memory limits and disables
motion blur, DoF and chromatic aberration. A 60 target remains available. Full RAM
preload stays off. This is not evidence of sustained 144 FPS gameplay. The local
displays expose 1440p/180 Hz modes; no display settings are changed by setup.

A source-release packager includes tracked source and pinned submodules, excludes
local saves/binaries and emits SHA256SUMS. Publication is gated on the native,
Vulkan, Python, GUI and archive checks. The loader boundary test's process timeout
was raised from 5 to 20 seconds after the heavily loaded desktop produced a
cold-start timeout; the test still checks the same explicit loader errors.


Final 0.7.0 checks passed: 164 Python tests, 14 C++/Vulkan targets, all native C
regressions, SPIR-V validation and Vulkan readback. The complete editor was
constructed, its 44 binding rows and 11 effects inspected, settings imported and
Midnight pages rendered to local screenshots. The source archive was extracted
outside the Git checkout and its CLI, shell scripts, pinned submodules, CSS and
FSR patch state checked; SHA-256 matched. The supplied game dump validated with
zero damaged DCX files and the 144 target profile was prepared. Sustained gameplay
frame rates remain a separate measurement, not a release claim.

## 0.7.1: native settings migration and setup failure analysis

The 0.7.0 complete-editor adapter required a second GTK window. All original
controls now belong to native Midnight pages, with a separate pure projection
module (`cachyborne_options.py`) and native panel mixin. Original source constants
are compared in tests so a missing setting/binding becomes a regression failure.

The post-DCX failure was a type-contract mismatch: `mods.selected()` returns
folder-name strings while `mods.build_overlay()` consumes `(name, root)` pairs.
Unpacking a long name raised “too many values to unpack”; a two-character name
could unpack silently into incorrect paths. A shared `layers_for_game()` resolver
now supplies pairs to both entry points, preserving sibling/direct overlays,
selection, disable switches and load order. Pipeline regression tests exercise
real resolution/overlay creation after simulated successful DCX validation.

Draft persistence precedes normalization and validation. Incomplete widgets can
be saved without running ingestion; preparation validates them separately and
publishes one successful generation last. Personal defaults include original
settings, INI/bindings and content selections. FSR 4 is the distribution default,
with explicit preflight asset checks. The local user's FSR 4 Native AA selection
is preserved rather than replaced with a lower preset. Full RAM staging remains
off for 16 GB, and a native internal image above 1080p still uses the larger
9152 MiB direct pool; RAM, PCIe and GPU work retain physical latency.
