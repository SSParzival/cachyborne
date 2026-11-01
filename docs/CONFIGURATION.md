# Native Midnight configuration

All settings are edited in one GTK4/Libadwaita Midnight window. The legacy
bbport window is retained as source/reference for compatibility tests; the new
interface neither instantiates it nor requires importing from it. The interface
uses Spanish labels. Game language and game-menu language remain independent
editable settings; the menu language field does not translate the GTK interface.

| Original configuration | Native page | Prepared behavior |
| --- | --- | --- |
| Game path | Preparar juego | Validates the executable and DCX files before publishing |
| Save, mod and patch paths | Mods y parches | Persistent save directory; selected content |
| Monitor and Vulkan GPU | Avanzado | SDL display name/index and Vulkan device selection; hardware detection button lists valid values |
| Fullscreen, HDR, presentation | Imagen y sonido / Rendimiento | Native presentation and optional Gamescope wrapper |
| Output/internal resolution, aspect ratio, live resolution | Imagen y sonido / Avanzado | Preset changes derive internal dimensions automatically; custom sizes remain editable; AMD GTT capacity checked |
| FPS mode and custom cap | Imagen y sonido | Original 30/60/90 modes and uncapped patch with custom cap for higher targets |
| FSR 3.1, FSR 4, FSR 4.1.1, TAA, DLSS, quality and RCAS | Imagen y sonido / Avanzado | INI and external asset selection; FSR 4 download, DLL-based FSR 4.1.1 preparation, Linux DLSS import/removal |
| Motion vectors, FPS counter, LOD, effect/debug switches and the AA selector | Imagen y sonido / Avanzado | Original settings and patch compiler |
| Game text, voice and game-menu language | Imagen y sonido | Installed message archives only; Spanish default and separate menu locale |
| Controller and all 45 keyboard/pad bindings | Controles | SDL names, native capture/reset buttons, sensitivity/inversion; empty binding explicitly unassigns |
| Mods and load order | Mods y parches | Ordered switches and move buttons; last enabled mod wins |
| XML patch selection | Mods y parches | Version-filtered switches; selection snapshotted per setup generation |
| Draw pipeline and GPU readbacks | Avanzado | Native tuning fields, including precise readbacks (`2`); retired memory selectors are migrated away |
| MangoHud, frame stats, logs, crash diagnostics, GPU profile, Vulkan validation | Avanzado | Runtime flags; disabled presence flags are removed rather than set to `0` |
| Extra environment variables | Avanzado | Quoted `KEY=value` values supported; names validated before setup |
| Online pool, BBSC, endpoints, UPnP, NPID/password and THD identity/token | Online y cuentas | Local configuration, explicit connection check and sign-in/sign-out |
| P/E placement, RAM staging, GameMode, Gamescope, scheduler | Rendimiento | CachyOS options preserved alongside original settings |

A draft is saved automatically after 700 ms without edits, on explicit save,
and when closing the window. Invalid/incomplete text is kept as a draft; setup
refuses it without discarding the last prepared generation. Changes apply to
the game after successful setup. Direct launch uses the last prepared profile.
Mods and patch selection are part of the draft/personal default snapshot and
are compiled/materialized only at setup time.

Personal defaults live in `~/.config/cachyborne/defaults.json` and are created
with **Guardar como mis predeterminados**. **Restaurar mis predeterminados** loads
them into the form. Configuration files are atomically replaced with mode 0600;
passwords and tokens are not bundled or committed. Keep mod source directories
in place while using a prepared symlink overlay.

Regression tests compare the native schema against every original setting and
binding. A real GTK child process checks native field/effect coverage, content
ordering and form persistence; pure tests check runtime projection and the
setup-to-overlay contract that previously failed after a successful DCX scan.
Network tests do not contact multiplayer services. GPU support and actual scene
load still govern available upscalers, presentation and frame rate.

## 0.7.3: independent speech and text

| Preference / variable | Default | Meaning |
| --- | --- | --- |
| `original_settings.language` / `BB_LANGUAGE` | `20` | PS4 text language: Latin American Spanish; missing Spanish archives fall back to `3`, then `1` |
| `voice_language` / `BB_VOICE_LANGUAGE` | `eng` | Speech banks and cutscene audio; `eng`, `esa`, `ese`, `jaj`, `ded`, `frf`, `iti`, `ptb` |
| `original_settings.ui_language` / `BB_MENU_LANGUAGE` | `es` | Port-owned in-game menu locale |
| `BB_AUDIO_TRACE` | unset | `1` logs FSB redirects and the existing audio diagnostics |

Use **Imagen y sonido → Textos y voces**,
or `cachyborne-setup GAME --voice-language eng`. Save and prepare again to
publish changes. File interception only redirects recognized suffixes inside
the retail `dvdroot_ps4/sound/` tree, and only to an existing nonempty regular
file. Missing alternatives preserve the requested bank. Invalid raw runtime
voice codes fall back to English; invalid saved preferences are rejected.
Audio streams use ISO-639 equivalents (`spa`, `jpn`, etc.) while retaining the
guest's video/text stream selection. If no matching audio stream exists, the
original selection remains available. CUSA03173's `sprj_opening.mp4` has eight
AAC tracks tagged `und`; the runtime supplies a fallback table only for the
99,240,674-byte asset with that nine-stream layout. English is the second audio
track (stream index 2), rather than the first Japanese track. No assets are rewritten.

The recommended button applies the complete 1440p Quality preset, including
English voices and Spanish text; it preserves bindings, content, HDR and paths.
The 16 GB profile rejects internal scenes above 1920×1080 regardless of sysfs
availability. `BB_DMEM_MB=5056` is the runtime's direct-pool variable;
`BB_DIRECT_MEMORY` is not a supported alias. Extra direct pool and resource
cache are each 256 MiB; `BB_MEMORY_WILLNEED=0` suppresses aggressive prefetch.

The launch plan also requests `BB_ASYNC_SUBMIT=1`, `BB_VK_RECORD_THREADS=3`,
`BB_LAYER_MEMORY=1` with the upstream 0.7 unified memory model, P CPUs `0-11`, E CPUs `12-19`, and a
4G Mesa cache at `$XDG_CACHE_HOME/cachyborne/mesa` (default `~/.cache/...`).
RADV defaults request SAM and 16× anisotropy. Firmware ReBAR and actual
performance must be checked separately. Explicit environment overrides remain
supported for diagnostics; use `cachyborne-launch --dry-run` to inspect them.

GUI checks are preliminary and debounce edits by 350 ms; setup performs the
full dump scan. Missing or invalid FSR 4 assets produce a warning and a prepared
FSR 3.1 profile. The chosen fallback is persisted in that generation. CSS
registration and its high-contrast callback skip absent GDK displays.


## Runtime menus and desktop identity

The graphics overlay and native System pages now have an actual Spanish UTF-8 /
UTF-16 catalog. `es` no longer falls through to English. The locale selector in
Imagen y sonido accepts Spanish and English; the selected launcher
locale overrides an older `menu_language=en` in the prepared INI. Speech preferences do not participate in menu text selection.

`BB_LANGUAGE` also resolves recognized `dvdroot_ps4/msg/<locale>` and
`menu/<locale>` paths. For `20`, missing assets fall back to Spain then English;
shared regional folders are left intact. This handles the EU executable asking
for English/Spain paths while Latin American Spanish is selected.

The runtime exports `SDL_APP_ID=cachyborne-play` and sets matching SDL metadata.
The installed `cachyborne-play.desktop` advertises that window class and the
original square icon. `BB_ICON_BMP` supplies the matching native window surface
icon for backends supporting it. SVG/PNG icons are installed without a network
request. Optional official-artwork downloads preserve aspect ratio with padding.
Regenerate old prepared plans with setup after updating.

## 0.8.2: upstream bbport 0.7 parity

The stable baseline is `0fc2921c45f8ff3900f6bfa43a299e9a4f532af4` (tag `0.7`).
Upstream split its editor: `bbport_launcher.py` imports `DEFAULTS`, `CONTROLS`
and `INI_DEFAULTS` from `bbport_launcher_common.py`. AST regression checks read
that actual definition module without importing the old window. Midnight has
exactly the 38 current `DEFAULTS` keys, translated control titles and identical
control IDs/default mappings. `trophy_key` is the only newly added DEFAULTS key;
INI settings and bindings have their own native schema, as upstream does.

| Upstream setting | Native schema / persistence | Midnight page | Runtime / setup projection | Behavior |
| --- | --- | --- | --- | --- |
| trophy_key | `LEGACY_DEFAULTS.trophy_key` | Online y cuentas → Trofeos locales | `BB_TROPHY_KEY` | 32 hex digits; private, validated and redacted from logs. |
| walk | `CONTROL_BINDINGS: walk` | Controles → Asignación de teclado y mando | `bbport.ini: key.walk` | Left Alt; keyboard-only slow walk. 45 assignable fields total. |
| game_text_language | `INI_OPTIONS.game_text_language` | Imagen y sonido → Textos y voces | `bbport.ini`, language-table patches; effective `BB_LANGUAGE` | Explicit text language across retail regions; auto follows the existing PS4 text selector. |
| game_voice_language | `VOICE_GAME_CODES[voice_language]` | Imagen y sonido → Textos y voces | `bbport.ini` + `BB_VOICE_LANGUAGE` | One native voice selector drives both upstream language tables and fork FSB/AAC interception; auto follows the game. |
| upscaler=fsr1 | `upscaler` | Imagen y sonido → Pantalla y escalado | `bbport.ini: upscaler` | FSR 1 spatial EASU/RCAS; included in CLI setup choices. |
| output_res=auto | `output_auto` + `output_res` | Imagen y sonido → Usar resolución del monitor | `bbport.ini: output_res=auto`, `BB_MONITOR_SIZE` | Monitor probed only during setup; fixed prepared dimensions preserve fast direct launch. Prepare again after switching monitors. |
| anti_aliasing | `INI_OPTIONS.anti_aliasing` | Imagen y sonido → Calidad y generación de cuadros | `bbport.ini: anti_aliasing` | auto/game/fxaa/smaa/off; upstream patch compiler derives effect_game_aa. SMAA requires no upscaler. |
| texture_quality | `INI_OPTIONS.texture_quality` | Imagen y sonido → Calidad y generación de cuadros | `bbport.ini: texture_quality` | auto/full/half/quarter; optional diagnostic override BB_TEXTURE_QUALITY. Auto halves textures with ≤3 GiB VRAM. |
| vbao | `INI_OPTIONS.vbao` | Imagen y sonido → Calidad y generación de cuadros | `bbport.ini: vbao` | GT-VBAO; needs ambient occlusion enabled. No invented BB_VBAO environment alias. |
| frame_generation | `INI_OPTIONS.frame_generation` | Imagen y sonido → Calidad y generación de cuadros | `bbport.ini: frame_generation` | off/fsr3/dlss; FSR 3.1 is experimental. Old 0/1 values migrate. Diagnostic override BB_FRAME_GEN. |
| frame_generation_multiplier | `INI_OPTIONS.frame_generation_multiplier` | Imagen y sonido → Calidad y generación de cuadros | `bbport.ini: frame_generation_multiplier` | 2/3/4, limited by the GPU and DLSSG runtime; override BB_FRAME_GEN_MULTIPLIER. |
| low_latency | `INI_OPTIONS.low_latency` | Rendimiento → Latencia y preparación | `bbport.ini: low_latency` | off/on/boost; NVIDIA Reflex or AMD Anti-Lag 2 when the driver exposes it; override BB_LOW_LATENCY. |
| camera_speed_x / camera_speed_y | `INI_OPTIONS.camera_speed_x/y` | Controles → Cámara con mando | `bbport.ini: camera_speed_x/y` | Finite values 0.25–2.00; independent stick camera axes. |
| fsr4_model | `INI_OPTIONS.fsr4_model` | Avanzado → FSR 4.1 · modelo | `bbport.ini: fsr4_model` | auto/fp8/int8/int8_rdna2/int8_lite; requires compatible captured assets. |
| light_grid_opt | `INI_OPTIONS.light_grid_opt` | Rendimiento → Latencia y preparación | `bbport.ini: light_grid_opt` | Light-grid optimization; raw diagnostic override BB_LIGHT_GRID_OPT. |
| FSR 4.1.2 driver models | `fsr412_driver`, `fsr411_dll`, `fsr411_dir` | Avanzado → Assets de escaladores | `build_assets.sh DRIVER UPSCALER`; `BB_FSR411_OUT` / runtime `BB_FSR411_DIR` | Accepts a user-provided AMD driver DLL or installer; never downloads proprietary DLLs. |
| DLSS Frame Generation library | `dlss_fg_library` | Avanzado → Assets de escaladores | Linux ELF `libnvidia-ngx-dlssg.so.*` in the user DLSS folder | Install/remove separate from Super Resolution; RTX 40+ for 2x, RTX 50 for higher multipliers. |
| Shader recipes / image shapes | `shader_recipes`, `image_shapes` | Avanzado → Compilación anticipada de shaders y texturas | `BB_SHADER_RECIPES`, `BB_IMAGE_SHAPES`, `BB_SHADER_GAME_DIR` | Optional seed files; no game code/data bundled. Cached recipes are validated by the runtime. |

An explicitly disabled game AA from pre-0.7 profiles migrates to `anti_aliasing=off`.
New choices participate in the same 700 ms autosave, explicit save and personal
defaults path. Numeric camera values reject NaN/infinity and out-of-range input;
invalid drafts remain saved, and invalid spin values cannot crash window creation.
All INI choices are validated before setup publishes a new prepared generation.

`readbacks`, `preupload`, `memory_model` and `as_0_3` left upstream's launcher
DEFAULTS. Readbacks remain supported by the runtime: the native `readbacks` tuning
field retains values from old profiles, including precise mode `2`. The removed
memory-model, guest-in-place, 0.3 and preupload variables are purged from saved
projections and inherited environments. They cannot re-enable the deleted model.
`BB_HUGEPAGES=0` still disables huge-page advice; prefetch is opt-in via
`BB_MEMORY_WILLNEED=1` and is never applied to the entire sparse backing arena.
The 16 GB profile retains its 5056 MiB pool and 256 MiB extra/resource budgets.
Texture packs requiring upstream's 9152 MiB pool fail setup with a budget error;
the previous prepared profile remains usable.

Cachyborne keeps fixed startup resolution in its recommended profile; upstream's
live resolution path remains selectable in Avanzado. With P/E pinning enabled,
upstream's automatic core-reservation monitor yields to that policy unless
`BB_CORE_RESERVE` is explicitly selected through advanced environment settings.

Windows-only `fsr_amd` DLL dispatch and WPF are not offered as Linux controls.
Their portable stubs and shared GPU declarations are retained where required
by the renderer. Linux runs FSR 4.1 models through Vulkan replay assets.
The complete tuning/debug variable catalog is [ENVIRONMENT.md](ENVIRONMENT.md);
raw expert overrides remain accessible through Avanzado → Variables adicionales.

## 0.8.2: diagnóstico optativo y caché

| Preferencia | Página | Tipo / predeterminado | Variable / comportamiento |
| --- | --- | --- | --- |
| fault_wait | Avanzado → diagnóstico | bool / false | BB_FAULT_WAIT=1 conserva el proceso tras un fallo para capturarlo con GDB |

Se guarda mediante el autosave de 700 ms, guardado explícito y Guardar como mis
predeterminados en defaults.json. Prepara de nuevo para publicar el cambio.
No añade claves al schema de referencia ni requiere GDB durante el uso normal.
`bash tools/debug_attach.sh PID` genera un informe privado y se desconecta;
`--terminate` solicita terminar ese proceso. Véase la
[revisión de comunidad](COMMUNITY_AUDIT_0.8.2.md).

La caché registra la huella del compilador de shaders y reserva los índices
persistidos aun cuando un shader no se precargue. Cambios incompatibles provocan
recompilación automática; no hay una opción adicional que el usuario deba ajustar.

## FSR SDK 2.4 assets and Sweet Purple icons

The native default is FSR 4.1 replay (`upscaler=fsr411`, a retained settings key),
Quality, 1706×960 → 2560×1440, model `auto`. Assets downloaded and generated by
`tools/fetch_fsr412_assets.sh` live in `$XDG_DATA_HOME/cachyborne/fsr4_412`.
The official source DLL is AMD 4.1.2.4555 from SDK 2.4.0 and stays outside Git.
The Windows-only SDK Vulkan loader is not loaded into the Linux game: native
SPIR-V replay executes on RADV. See [upscaling](upscaler.md) for validation limits.

Game and configuration shortcuts install separate original SVG/PNG icons using
Sweet Purple colors, plus the SDL BMP. Reinstall with
`./cachyborne-install.sh --user --skip-build` after building. User-selected game
assets, saves, mods and server credentials are preserved by the updater.
