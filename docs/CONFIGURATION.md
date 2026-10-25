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
| Output/internal resolution, aspect ratio, live resolution | Imagen y sonido / Avanzado | Explicit internal resolution; use Calcular resolución interna desde el preset after changing quality to derive it |
| FPS mode and custom cap | Imagen y sonido | Original 30/60/90 modes and uncapped patch with custom cap for higher targets |
| FSR 3.1, FSR 4, FSR 4.1.1, TAA, DLSS, quality and RCAS | Imagen y sonido / Avanzado | INI and external asset selection; FSR 4 download, DLL-based FSR 4.1.1 preparation, Linux DLSS import/removal |
| Motion vectors, FPS counter, LOD and all 11 original effect/debug switches | Imagen y sonido / Avanzado | Original settings and patch compiler |
| Game and game-menu language | Avanzado | PS4 language code and menu locale |
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
