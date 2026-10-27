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
| Motion vectors, FPS counter, LOD and all 11 original effect/debug switches | Imagen y sonido / Avanzado | Original settings and patch compiler |
| Game text, voice and game-menu language | Imagen y sonido | Installed message archives only; Spanish default and separate menu locale |
| Controller and all 44 keyboard/pad bindings | Controles | SDL names, native capture/reset buttons, sensitivity/inversion; empty binding explicitly unassigns |
| Mods and load order | Mods y parches | Ordered switches and move buttons; last enabled mod wins |
| XML patch selection | Mods y parches | Version-filtered switches; selection snapshotted per setup generation |
| Draw pipeline, GPU readbacks, preupload, memory model and 0.3 behavior | Avanzado | Exact original runtime values, including Precise readbacks (`2`) |
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
`BB_GUEST_IN_PLACE=1`, `BB_LAYER_MEMORY=1`, P CPUs `0-11`, E CPUs `12-19`, and a
4G Mesa cache at `$XDG_CACHE_HOME/cachyborne/mesa` (default `~/.cache/...`).
RADV defaults request SAM and 16× anisotropy. Firmware ReBAR and actual
performance must be checked separately. Explicit environment overrides remain
supported for diagnostics; use `cachyborne-launch --dry-run` to inspect them.

GUI checks are preliminary and debounce edits by 350 ms; setup performs the
full dump scan. Missing or invalid FSR 4 assets produce a warning and a prepared
FSR 3.1 profile. The chosen fallback is persisted in that generation. CSS
registration and its high-contrast callback skip absent GDK displays.


## 0.7.4: Spanish runtime menus and desktop identity

The graphics overlay and native System pages now have an actual Spanish UTF-8 /
UTF-16 catalog. `es` no longer falls through to English. The locale selector in
Imagen y sonido accepts Spanish, English and Russian; the selected launcher
locale overrides an older `menu_language=en` in the prepared INI. The previous
Cyrillic-versus-English guess has been removed: Latin script cannot identify a
language. Speech preferences do not participate in menu text selection.

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
