# Cachyborne 0.8.4

Focused on the native **2560×1440 (`1x`, `preset=0`), FSR 4.1 (`FP8 t2160_m0`), 60 FPS** profile on Intel Core i5-14600KF (`P-cores 0-11`, `E-cores 12-19`), AMD Radeon RX 9070 XT (RADV GFX1201) and 16 GiB RAM on CachyOS Linux.

## Highlights

- **Default 1440p Native AA (`preset=0`) + FSR 4.1 (`fsr411`) + 60 FPS Profile**:
  - `DEFAULTS` and `performance_settings(target='60')` in `launcher/cachyborne.py` default to `2560x1440` render and output resolution, `upscaler='fsr411'`, `preset='0'` (1x Native AA), `fps='60'`, `fps_limit=60`, `present_mode='mailbox'`, `BB_GUEST_VRAM='0'`, and `NATIVE_60_ENV` (`BB_GPU_SPIN_US='0'`, `BB_PIPE_SPIN_US='0'`, `BB_LIMITER_MARGIN_US='auto'`, `BB_PACE_STABLE='1'`).
- **BBHOST (`v0.2.26` / `bdc5cdf` + Open PRs) & Native Fork Convergences**:
  - **Frame Pacing & Timing**: Auto-adaptive limiter margin (`BB_LIMITER_MARGIN_US=auto`), phase-stable 60 Hz flip pacing (`BB_PACE_STABLE=1`), 60 Hz ragdoll/cloth damping clamp, character sync & replay time scaling, 60 Hz wind accumulator, opt-in 30 Hz water simulation gate (`BB_WATER_GATE=1`), and opt-in Bloodletter transformed-L2 self-hit radius correction (`BB_BLOODLETTER_SELF_HIT=1`).
  - **GX / Command Processor / Memory & Shader Cache**: GX job-barrier spin release (`BB_GX_JOB_BARRIER=1`), GX command-buffer block reclaim & rename-wait CPU-label flush (`BB_GX_RECLAIM=1`), opt-in fresh occlusion query clear (`BB_OCCLUSION_FRESH=1`), PR #117/#126 zero-overhead dispatch trace gating, `BlobStore::Flush()` visibility barrier before shader recipe import, resident `Program` specialization-owner rebasing, and target-isolated shader fingerprint (`ShaderFingerprint.cmake`).
  - **Engine & Scaleform Crash Guards (`gpu/games/bloodborne/sf_text_layout.{h,cpp}`)**:
    - Retail 1.09 ABI-verified `Scaleform::Render::TreeCacheText::UpdateTransform` (`0x12d5e0`) null `Text::Layout*` guard and deferred `TreeCacheRoot` re-queue (`BB_SF_TEXT_GUARD`, default on) + `HandleChanges` (`0x12df80`) layout-change tracking.
    - `GXSceneContext::Initialize` both-sides width/height display-buffer check (`BB_SCENE_BOTH_SIDES`, default on) and opt-in full-resolution water-wave target (`BB_WATER_WAVE_FULL=1`).
    - Default Worldwide network matching region (`BB_MATCHING_REGION`, default Worldwide) and configurable SOS bell sign timeout (`BB_SIGN_TIMEOUT`).
    - FMOD `Project::init` language-count zero-initialization (`BB_FMOD_PROJECT_INIT=1`, PR #104).
  - **Game Enhancements & QoL**:
    - Hunter's Dream appearance mirror (`change_appearance` / `BB_CHANGE_APPEARANCE=1`).
    - Offline Altar of Despair rebirth with level-cost refund and undo (`rebirth` / `BB_REBIRTH=1`).
    - Pre-order Messenger hat gifts in Hunter's Dream (`messenger_gifts` / `BB_MESSENGER_GIFTS=1`, PR #203).
    - Confirmed save-and-quit to desktop (`save_quit.cpp`, `runtime_quit.c`).
    - Offline & live-verified `param_tables.py` TOML mod generator and `Params::ListIds` row-ID reader.
    - Native v1 12-service plugin host prefix (`bbport_plugin_*`, `runtime_plugin_hle.*`).
    - THD UDP large datagram & chunk-boundary splitting (`BB_NET_LARGE_DATAGRAMS=1`, `BB_NET_SPLIT_DATAGRAMS=1`).

## Verification Summary

- **Automated Test Suite (`scripts/check-all.sh`)**:
  - **124 / 124** C++ CTest targets passed (`0` skipped) against retail 1.09 `eboot.bin`.
  - **338** Python unit tests passed (`1` GTK display socket test skipped in sandbox).
  - All C runtime tests (`runtime-test`, `pad-test`, `quit-test`, `savedata-quit-test`, `mods-test`, `sema-test`, `online-test`, `content-test`, `affinity-test`, `trophies-test`) and RX 9070 XT Vulkan readback passed.
- **Live 120-Second Native 1440p (`1x`) FSR 4.1 (`FP8 t2160_m0`) 60 FPS Qualifications**:
  - **Hunter's Dream Movement & Resource Trace (`out/native-final-dream-120/manifest.json`)**: `7200` frames, `59.992 FPS` (`60.0 FPS` window), `p99 = 16.71 ms`, `worst = 16.73 ms`, `~1.38` single-core CPU equivalents (`~6.9%` of 20 threads), `1.15 GiB` RSS, `4.44 GiB` VRAM, all 15 acceptance gates passed.
  - **Nightmare of Mensis Physical DP-2 Desktop & PipeWire Audio (`out/native-final-physical-120/manifest.json`)**: `7200` frames, `59.991 FPS` (`60.0 FPS` window), `p99 = 16.71 ms`, `worst = 18.74 ms`, zero audio underruns, original user saves untouched, all 15 acceptance gates passed.
