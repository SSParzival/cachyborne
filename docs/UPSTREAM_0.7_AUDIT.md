# Stable bbport 0.7 integration provenance

Audit date: 2026-10-29. The worktree was clean at the start (`230df37`).
The configured upstream is https://github.com/deadinside28/bloodborne_pc.git.
`git fetch upstream --tags` and GitHub's releases/latest API identify stable
[0.7](https://github.com/deadinside28/bloodborne_pc/releases/tag/0.7), published
2026-10-28 23:58:29 UTC, commit `0fc2921c45f8ff3900f6bfa43a299e9a4f532af4`.
`upstream/master` pointed to that same commit at audit time. The common ancestor
is release 0.6, `8185b88d80573fa300595d5c54450183b4109935`. Upstream has one
release commit between these points; its message groups the intervening work.
The upstream comparison covers 962 files, including moved code and new notices.

## Runtime and renderer

- Ported split runtime_memory and runtime_pad modules with their callers: unified
  memory, sealed memfd backing, lazy GPU imports, pinned-page accounting, heap
  growth/commit/decommit, CPU write preparation and remote file support. Removed
  obsolete NGS2/toypad modules exactly where upstream removed their registrations.
- Ported the shader translator, buffer-cache/layer split, pipeline compiler/import
  split, shader recipe rebuild, texture spare pool and image-shape cache together
  with their headers and tests. Includes occlusion-query scoping, ring deduplication,
  copy-shader filtering, point-light shadow and item-icon fixes, cross-heap vertices,
  cloth timing above 30 FPS, save-switch regressions and crash-site fixes.
- Ported FSR 1, SMAA/FXAA2_GS, GT-VBAO, FSR optical-flow/interpolation shader
  variants, FG color/HDR handling, camera matrices and motion-vector changes,
  DLSSG bridge support, FSR 4.1.2 capture/replay and low-latency backends.
- Ported upstream audio/AvPlayer tracing and runtime services while retaining
  independent retail FSB and AAC voice selection and Spanish text interception.
- Ported bbnet20/BBSC/THD relay, persistence and network fixes. Removed the old
  bbnet/shadp2p tree and build target. Seamless Hunt addresses and user-selected
  endpoints remain unchanged. Tests resolve online imports locally, without
  authenticating to or contacting matchmaking services.
- Ported local trophies.jsonl, trophy metadata/decryption, and native Midnight
  trophy viewing. OpenSSL libcrypto is optional for names/icons; numeric unlock
  records still work without it.

## Scripts, baseline and scope

Upstream launcher/helper modules and settings_schema.json are retained as
reference. Only direct execution of bbport_launcher.py is redirected to Midnight;
the window is never instantiated by the native settings workflow. The reference
adapter follows the moved helper definitions. Copyright notices and NOTICE are
retained. Setup tools include texture-pack detection, save backup when mod sets
change, language-table patches and FSR 4.1.2 driver-model extraction.

The portable GPU source includes Windows guards/stubs where upstream's APIs
require them. Windows WPF, Windows packaging, Windows benchmark-only tools,
upstream run.py/run.sh's prepare-on-launch workflow and GCC PGO are deliberately not installed into Cachyborne's Linux workflow.
The native installer, CachyOS branding and source-distribution mechanism remain
authoritative. Optional Nix package payload lists were adjusted for bbnet20 and
DLSSG; AppImage/Nix/Windows distribution builds are not part of validation here.

## Collisions and resolutions

| Collision | Resolution |
| --- | --- |
| Upstream PGO/build settings vs native toolchain | Kept Linux-only Clang, -O3/-march=native, mold, ThinLTO and bounded build jobs. Added the new shader/test targets and DLSSG copy logic. |
| Monolithic memory patch vs new split and sparse heaps | Moved huge-page/committed low-memory advice into the new structure; kept BB_HUGEPAGES and opt-in WILLNEED. Never prefetch the sparse pool or uncommitted heap tail. |
| Upstream core-reservation thread vs P/E masks | Native pinning wins by default. Compiler thread naming moved into pipeline_cache_internal.h; ShaderImport uses E cores too. |
| Spanish menu and icon hunks vs split overlay/HiDPI | Kept Spanish enum/catalog and explicit locale precedence; moved translated preset labels to overlay_sections; retained SDL identity/icons and added Wayland scaling. |
| Voice interception vs upstream language patches | One native selector writes both game_voice_language and BB_VOICE_LANGUAGE. Auto follows guest selection; text remains independent. |
| Old mod helper vs upstream backup/link helpers | Retained layers_for_game and its tuple contract; added backups and upstream overlay linking without moving ingestion into launch. |
| Removed launcher settings | Exact current DEFAULTS key parity, retired env cleanup, precise readbacks migrated to a fork tuning field. |
| New SDK callback references missing from the pinned backend | Added reproducible FSR patch 0003 for shader override and job timestamps; original SDK reflection and fallback shaders remain intact. |
| Upstream test assumptions | Kept prepared-launch tests; extended GPU stubs for lazy heaps; corrected persisted FSR frame-gen expectations to match shipped 0.7 runtime semantics. |
| Standalone CMake with private Zydis | Resolve the exported bare Zycore dependency to its configured absolute library; no shell-only linker-path requirement. |

No git merge, cherry-pick or wholesale upstream checkout was used. Upstream
components were compared against the common ancestor in temporary candidates;
local collisions were resolved per file before copying each component. The
large diff includes upstream source splits and restored attribution notices.
The [configuration inventory](CONFIGURATION.md#080-upstream-bbport-07-parity)
lists each new option, native owner, page and real projection.

## Verification

See [current validation](VALIDATION_0.8.2.md) for exact commands and results.
Automated ABI, shader and device tests do not establish full-game performance
or real-world multiplayer interoperability. No game assets were changed.
