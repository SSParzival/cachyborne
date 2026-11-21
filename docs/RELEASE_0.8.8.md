# Cachyborne 0.8.8

Native Linux x86-64 Bloodborne runtime and GTK4/Libadwaita control center (**Midnight**), built for CachyOS/Arch Linux and AMD RDNA 4 / Mesa RADV.

## What's New in 0.8.8

- **New Default Performance Profile (90 FPS base → 180 FPS target)**:
  - Base frame rate defaults to **90 FPS** (`fps=90`, `fps_limit=90`) with **FSR 3.1 2× Frame Generation** enabled (`frame_generation=fsr3`, `frame_generation_multiplier=2`) for a **180 FPS** display target on any Vulkan GPU.
  - **Adaptive Sync / VRR** (`vrr=true`) is enabled by default across Midnight, the upstream schema (`launcher/settings_schema.json`), and the reference launcher.
  - **Dynamic and high-quality shadows disabled by default**: `effect_dynamic_shadows=0`, `contact_shadows=0`, and `pcss=0` reduce draw calls and compute overhead; all three remain toggleable in the launcher or in-game menu.
  - Added `--performance-profile 90` to `cachyborne-setup` (alongside `60` and `144`).
- **Automatic Previous-Config Cache Purge (No Extra Disk/RAM Accumulation)**:
  - When `cachyborne-setup` publishes a changed configuration (`auto_prune=true`, default), it automatically deletes superseded `prepared/` generations, stale `/dev/shm` ramdisk trees (pruned *before* staging new ramdisk copies so RAM is never doubled), and the previous configuration's `shader_cache/` and Mesa RADV shader cache (`~/.cache/cachyborne/mesa`).
  - Re-running setup with identical settings preserves valid active shader/Mesa caches while still cleaning temporary `.tmp` and `previous/` files.
  - Failed or cancelled setups leave the previous prepared profile and caches intact.
  - `bbport_launcher.py` and `bbport_launcher_content.py` also purge obsolete shader/Mesa caches when saved settings, mods, or patches change.
  - User saves (`savedata/`, `savedata-backups/`), trophies, and user-provided upscaler libraries (`dlss/`, `fsr4_412/`) are never deleted.
- **Fork Performance Audit (`docs/FORK_PERFORMANCE_AUDIT_2026-11-21.md`)**:
  - Reviewed **bbhost** (`droogie/bbhost`), **Lantern Engine** (`Awakenchan/LanternEngine`), **Supermedo** (`Supermedo/bloodborne_pc`), and **bbport** (`deadinside28/bloodborne_pc`, `bloodborne-runtime`, `ayozetr`, `Mrsuss60`) against Intel i5-14600KF + Radeon RX 9070 XT (16 GB VRAM, RADV) + 16 GB RAM on CachyOS, documenting what is already integrated, what is platform/VRAM-specific, and future candidates requiring live in-game profiling.

---

## Requirements

- **OS**: CachyOS or Arch Linux (x86-64). Other Linux distributions can build directly with `bash build.sh` once Clang, mold, Ninja, CMake, Vulkan, SDL3, GTK4, Libadwaita, and development libraries are installed.
- **GPU**: Vulkan 1.3 capable GPU (tested primarily on AMD RDNA 4 / Mesa RADV; NVIDIA and other Vulkan GPUs are supported via FSR 3.1 / optional DLSS).
- **Game files**: Your own legally obtained, **decrypted Bloodborne 1.09 dump** (`CUSA03173` or any retail region with the 1.09 update and DLC merged, containing `eboot.bin`, `sce_sys/param.sfo`, and `dvdroot_ps4/`).
  - **No copyrighted game files, saves, or proprietary AMD/NVIDIA binaries are included in this release.**

---

## How to Install and Use

### 1. Download and Verify

Download `cachyborne-0.8.8-source.tar.gz` and `SHA256SUMS` from the release assets below, then verify and extract:

```bash
sha256sum -c SHA256SUMS
tar -xzf cachyborne-0.8.8-source.tar.gz
cd cachyborne-0.8.8
```

*(Alternatively, clone from Git with submodules: `git clone --recursive https://github.com/SSParzival/cachyborne.git && cd cachyborne`)*

### 2. Build and Install

Run the installer as your normal desktop user (not root; `pacman` will ask for `sudo` to install missing build/runtime packages):

```bash
./cachyborne-install.sh
```

What this does:
- Installs required Arch/CachyOS dependencies (`clang`, `mold`, `cmake`, `ninja`, `sdl3`, `gtk4`, `libadwaita`, `vulkan-radeon`, `libdeflate`, etc.).
- Compiles the native runtime (`out/bb-probe`), Vulkan GPU library (`out/gpu/libbbgpu.so`), and online module (`out/gpu/libbbnet20.so`) with Clang, `-march=native`, ThinLTO, and `mold`.
- Links `cachyborne-gui`, `cachyborne-setup`, and `cachyborne-launch` into `~/.local/bin` and installs separate desktop shortcuts for playing and configuring.
- Keep the extracted directory in place, as `~/.local/bin` symlinks point to it. Make sure `~/.local/bin` is in your `PATH`.

> If dependencies are already installed, run `./cachyborne-install.sh --user`. To install only shortcuts after compiling manually, run `./cachyborne-install.sh --user --skip-build`.

### 3. Optional: Extract FSR 4.1.2 Assets (AMD RDNA 3/4)

Default upscaling uses **FSR 4.1** (`fsr411`) with automatic fallback to **FSR 3.1** if assets are not present. To build local Vulkan SPIR-V replay assets from AMD's official SDK 2.4.0 DLL (requires Proton + MinGW/SPIR-V tools; the Windows DLL is only used during extraction):

```bash
bash tools/fetch_fsr412_assets.sh
```

### 4. Configure and Prepare Your Game Dump

**Option A — Graphical Interface (Midnight):**

```bash
cachyborne-gui
```

1. Open **Preparar juego** and select your decrypted `app0` / `CUSA03173` folder.
2. Adjust settings in **Imagen y sonido**, **Rendimiento**, **Controles**, **Mods y parches**, or click **Aplicar perfil recomendado** on the home screen (90 FPS base, 2× FG = 180 FPS, VRR on, dynamic/high-quality shadows off, 1440p).
3. Click **Validar y preparar** to validate the executable/DCX archives, compile patches, and atomically publish your active profile.

**Option B — Command Line:**

```bash
# Default 90 FPS + 2x FSR 3.1 FG (180 FPS target) + VRR + high shadows off:
cachyborne-setup /path/to/decrypted/CUSA03173 --performance-profile 90

# Or fixed 60 FPS / 144 FPS profiles:
cachyborne-setup /path/to/decrypted/CUSA03173 --performance-profile 60
cachyborne-setup /path/to/decrypted/CUSA03173 --performance-profile 144
```

### 5. Launch the Game

Inspect the prepared launch command and environment without starting the game:

```bash
cachyborne-launch --dry-run
```

Start Bloodborne:

```bash
cachyborne-launch
```

Or use the **Bloodborne — Cachyborne** desktop/menu shortcut (or `./cachyborne-launch.sh` directly from the project folder).

- **In-game settings overlay**: press `Insert` on keyboard or hold `L3 + R3` on controller.
- **Mouse look**: click inside the game window to capture the mouse; press `F1` to release.
- **Reclaim disk space manually**: run `python3 launcher/cachyborne.py clean` or click **Liberar espacio** in Midnight's Home screen (saves are always preserved).

---

## Default Settings Summary

| Setting | Default |
| --- | --- |
| Base FPS / Cap | `90` (`BB_FPS=90`, `BB_FPS_LIMIT=90`) |
| Frame Generation | `fsr3` × `2` (180 FPS display target) |
| Adaptive Sync / VRR | Enabled (`vrr=true`) |
| Dynamic / Contact / PCSS Shadows | Disabled (`effect_dynamic_shadows=0`, `contact_shadows=0`, `pcss=0`) |
| Upscaler / Preset | FSR 4.1 (`fsr411`, falls back to FSR 3.1 if assets absent) · Native AA (`preset=0`), 2560×1440 |
| Present Mode | `Mailbox` |
| CPU Pinning | P-Cores `0-11` (game/render/audio), E-Cores `12-19` (shader/decompression workers) |
| Memory Profile | `16gb` (disk assets/caches, no full tmpfs staging, 5936 MiB direct pool at 1440p) |
| Previous Config Cache Cleanup | Automatic (`auto_prune=true`), preserves `savedata/` |

---

## Verification

- `python3 scripts/env_vars.py --check`: exit `0`.
- `pytest -v`: exit `0` (`304 passed, 49 skipped, 1397 subtests passed`).
- `clang++ -std=c++23 -Wall -Wextra -I. -Igpu tests/test_settings.cpp gpu/shim/bbport_settings.cpp`: passed.
- Note: Live in-game performance benchmarks were not re-run for this release because no retail game dump is attached to this environment.

## License

Cachyborne includes adaptations from `bbhost` under **GPL-3.0-or-later** (`LICENSES/GPL-3.0-or-later.txt`, `NOTICE`) and `bbport` under **GPL-2.0-or-later** (`LICENSE`). Third-party components retain their respective licenses.
