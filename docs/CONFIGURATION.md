# Configuration coverage

The Midnight GTK control center provides a streamlined hardware/game setup view
and the **complete original bbport GTK editor**. Open it from Controles y extras,
edit options, choose Aplicar a Cachyborne, then run Validar y preparar. Closing
the complete editor also imports its settings as a draft. Launch continues to
use the last successfully prepared generation.

The original editor retains its existing language choices. The new control center
uses Spanish; game language is configured independently in the complete editor.

| Original bbport configuration | Where it remains available | Prepared behavior |
| --- | --- | --- |
| Game and save directories | Setup / Controles y extras / complete editor | Validated app0 and selected persistent user folder |
| Display, GPU, fullscreen, HDR, presentation, aspect ratio | Imagen y sonido and complete editor | Display/GPU environment, HDR and INI; includes FIFO Relaxed |
| FSR 3/4/4.1.1, DLSS, TAA, presets, RCAS and motion vectors | Imagen y sonido and complete editor | Full INI, selected asset paths and fixed/live resolution configuration |
| FSR 4.1.1 DLL tooling and DLSS library installation | Complete editor | Existing upstream tools and the selected user's library/assets folders |
| Effects, LOD, intro skip, debug camera/menu | Imagen y sonido and complete editor | Setup compiles matching game patches; original requirements still apply |
| FPS mode and custom cap | Imagen y sonido and complete editor | Native frame hooks and cap; recommended 144 target with a 60 alternative |
| Game language and launcher language | Complete editor | Game/menu environment; original editor language choices retained |
| Controller selection, 44 keyboard/gamepad bindings and mouse settings | Controles y extras and complete editor | Every key./pad. INI entry plus sensitivity/inversion |
| Mods, enable/disable and load order | Controles y extras and complete editor | Persistent selected overlay, prepared once |
| Third-party XML patch switches | Controles y extras and complete editor | Selected patches compiled during setup |
| Draw pipeline, readbacks, preupload and memory model | Complete editor | Matching runtime environment, including old-model comparison modes |
| MangoHud, statistics, saved logs, crash diagnostics, GPU profiling, Vulkan validation | Complete editor | Runtime flags and persistent logs; implicit-layer defaults allow enabled MangoHud |
| Extra environment variables | Complete editor | Validated variable names and safely quoted values |
| Online pools, BBSC, endpoints, UPnP, account identity and Discord sign-in | Complete editor | Original account tooling, prepared routing and selected online module |
| P/E CPU placement, RAM staging, GameMode, Gamescope and scheduler | Rendimiento | CachyOS-specific options retained independently of the upstream editor |

The adapter retains all **41 original settings defaults**, all **24 original INI
defaults**, and any additional key/value created by the original editor. Regression
tests check original-key coverage, custom bindings, game-side INI changes,
environment propagation and deterministic online configuration without network
calls. Original native control and online-module tests run in the full suite.

Online credentials remain in the user's private configuration. Diagnostic dry-run
output redacts token/password/auth fields. Setup validates the game even when
advanced launch variables are supplied. Online play itself depends on the chosen
service and user account; testing local configuration does not certify a community
server or multiplayer session.

The complete editor does not automatically start a community server fallback.
The renderer's actual device support still governs FSR/DLSS and presentation.
