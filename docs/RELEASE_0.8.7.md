# Cachyborne 0.8.7

Focused on frame-paced runtime efficiency, disk-space reclamation, archive validation, and a cleaner Bisexual Midnight GTK4 launcher, syncing upstream changes from `droogie/bbhost` (`v0.2.34`, `1cff2cb`) and `Awakenchan/LanternEngine` (`v0.9.42`, `573f83f`).

## Highlights

### Efficiency & Stability (`bbhost` `v0.2.34`)
- **Adaptive draw-pipe rest (`BB_PIPE_REST=auto|0|always`)**: Tracks `BbStats::frame_paced` in `gpu/shim/bbport_frame_rate.cpp`, `gpu/render/vk_draw_pipe.h`, and `gpu/render/rasterizer.cpp` using 250 ms hysteresis across two >=85%-paced windows (with `kRestBatch=64` and `kRestSpin=64`) to reduce CPU spin when capped at 60 FPS without sleeping through shader compiles or heavy bursts.
- **Parallel shader blob preloading (`BB_CACHE_READ_AHEAD`)**: `BlobStore::ReadAhead()` and `FinishPreload()` (`gpu/render/blob_store.h`, `gpu/render/blob_store.cpp`, `gpu/render/pipeline_store.cpp`) warm cached pipeline blobs across up to 16 worker threads (enabled by default; set `BB_CACHE_READ_AHEAD=0` to disable).
- **Idle VRAM retention & barrier clamping**: `BufferCache::HasIdleVramHeadroom()` (`gpu/buffers/buffer_cache.h`, `gpu/buffers/buffer_residency.cpp`, `gpu/layer/buffer_cache_layer.cpp`) and `GarbageCollectImages()` (`gpu/textures/texture_cache_memory.cpp`) retain idle allocations below 75% budget unless `BB_VRAM_IDLE_ALWAYS=1`; `gpu/textures/image.cpp` clamps `GetBarriers` mip/layer counts and clips unwatched partial-edge-page downloads (`BB_STORE_UNWATCHED=1`).
- **RADV indirect draw guard (`BB_INDIRECT_GUARD_MAX`)**: Clamps default `max_total` to `65535u` in `gpu/render/vk_indirect_guard.cpp` on RADV unless explicitly overridden.

### Compatibility & Validation (`LanternEngine` `v0.9.42`)
- **Map archive truncation check**: Added `archive_required_size` and `broken_archives` (`scripts/game_check.py`, plus `--archives` CLI flag and `problem()` integration) to catch missing or truncated `.tpfbdt`/`.hkxbdt` companions before launch.
- **Online language unlock patch**: Added opt-in `Unlock All Languages (Works Online)` metadata patch (`isEnabled="false"`) to `patches/Bloodborne.xml`.

### Disk Reclamation & Launcher UI
- **Storage management (`launcher/cachyborne.py`)**: Added `auto_prune` (enabled by default) to remove superseded `prepared/` profile generations after setup while keeping the active generation, alongside `prune_old_logs`, `prune_old_ramdisk`, stale cache cleanup (`_stale_cache_targets`), `_superseded_dist_files`, `disk_usage_summary`, `reclaim_disk_space`, and the `clean` CLI subcommand. Saves and active shader caches are always preserved.
- **Efficiency options (`launcher/cachyborne_options.py`)**: Added `aniso`, `pipe_rest`, `cache_read_ahead`, and `auto_prune` controls with validation, `KNOWN_ENV` mappings (`BB_ANISO`, `BB_PIPE_REST`, `BB_CACHE_READ_AHEAD`), and launch projection.
- **UI refinement (`launcher/cachyborne_gui.py`, `launcher/cachyborne_panels.py`, `launcher/styles/midnight.css`)**: Kept the Bisexual Midnight theme while removing synthetic banners and tightening widget layout, added real `_host_hardware_summary()` telemetry and live Home status, added the `"Espacio en disco"` card with `"Liberar espacio"`, batched setup logs every 60 ms, and guarded headless `Adw.StyleManager.get_default()` calls.
- **Build RPATH (`gpu/CMakeLists.txt`)**: Appended `$ORIGIN`, `$ORIGIN/../native-deps/usr/lib`, and `${CMAKE_BINARY_DIR}/../native-deps/usr/lib` to `CMAKE_BUILD_RPATH` so `libbbgpu.so`, `bb-probe`, and test executables resolve private-prefix `libminiz.so.3` and `libZydis.so.4.1` without requiring `LD_LIBRARY_PATH`.

## Verification
- `bash build.sh --test`: rebuilt `out/bb-probe`, `out/gpu/libbbgpu.so`, `out/gpu/libbbnet20.so`, and all standalone C/C++ test binaries cleanly.
- `python3 scripts/env_vars.py --check` & `bash -n`: passed.
- `ctest --test-dir out/gpu --output-on-failure -L cachyborne`: 76 passed, 47 skipped (retail ELF/environment unavailable); pre-existing `#88 plugin-host-test` fails without `CACHYBORNE_TEST_ELF`.
- `pytest -v`: 348 passed, 2 skipped (live display / retail dump hardware checks), 1469 subtests passed.
