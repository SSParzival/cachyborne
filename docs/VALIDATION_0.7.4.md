# Cachyborne 0.7.4 — validation, 2026-10-26

Hardware: i5-14600KF, RX 9070 XT / RADV GFX1201, Mesa 26.2.5-arch3.1,
16 GB RAM, KDE Plasma on Wayland. Tests used the CUSA03173 1.09 dump.

## Root causes corrected

`BB_MENU_LANGUAGE=es` previously fell through to English in a runtime that only
implemented English/Russian. Native System-page hooks also inferred English
from every non-Cyrillic string. An installed draft/plan still selected British
English (`BB_LANGUAGE=18`). Merely exporting an unsupported menu locale or
changing source defaults could not repair that already prepared launch.

The runtime now has Spanish overlay and UTF-16 native-page catalogs. Explicit
launcher locale overrides an older saved menu choice. Message, layout and font
resolution follows text locale independently of FSB banks/AAC speech. The real
profile, draft, personal defaults and direct execution plan were regenerated
with text `20`, menu `es` and voice `eng`; prior configuration was backed up
locally. Saves were retained.

## Regression and build results

| Check | Result |
| --- | --- |
| `bash build.sh --test` | Passed; C/C++ and GPU/shader compilation, native C checks |
| `bash scripts/check-all.sh` | Passed; 15/15 CTest, 195 Python tests, SPIR-V validation and device-backed Vulkan submission/readback |
| Headless Python discovery, DISPLAY and WAYLAND_DISPLAY unset | 195 tests, no failures/errors, one actual-window test skipped |
| Actual GTK native-window test | Passed in the normal suite, including controls, persistence and independent voice selection |
| `git diff --check` | Passed before commit |

New coverage exercises the actual native menu's UTF-16 labels/accents, English
and Spanish selection, dynamic aspect-ratio labels and voice independence.
Settings tests verify Spanish persistence and explicit locale precedence.
Native file-operation tests check Spanish message/layout routing, archive
fallback and independent voice-bank routing. Catalog checks cover literal
overlay labels and retain every printf placeholder. Icon tests verify offline
installation, the desktop identity and aspect-preserving portrait conversion.

## Actual game and visual checks

`python3 scripts/smoke-game.py --seconds 15` passed on the regenerated published
profile with `BB_FILE_TRACE=1 BB_AUDIO_TRACE=1 BB_GAME_MENU_TRACE=1`:

- RADV initialized on GFX1201; the CPU-read dma-buf microbenchmark reported
  **69,032 MB/s**. This is an initialization measurement, not a sustained
  end-to-end bandwidth guarantee.
- Traces show `menu/spaes/font.gfx` resolving to `menu/spaar/font.gfx` and both
  message archives resolving to `msg/spaar`. Speech independently resolved
  `sprj_main_ese.fsb` to `sprj_main_eng.fsb`.
- Two FPS intervals measured 135.9 and 144.0; the latter reported a worst frame
  of 8.5 ms. Startup is not uniformly 144 FPS and does not certify combat/4K.
- A GPU readback was visually inspected: the title menu shows Continuar,
  Cargar juego, Nuevo juego, Sistema and Iniciar sesión.

Additional 25- and 30-second smoke runs passed. Scripted controller input opened
Sistema and then the native Pantalla page. Compositor captures were inspected:
Sistema contains Pantalla/Efectos; the actual graphics page shows Relación de
aspecto, Automático, Resolución, Escalador, Preset de calidad, Calidad, Nitidez,
Detalle de modelos and Máximo. Accents render correctly and the rows fit the
layout. The window decoration shows the original hunter-sigil application icon.

The overlay catalog is covered by regression checks; an attempted scripted
overlay capture did not show that overlay, so the inspected screenshots are
not claimed as a visual certification of every overlay page. Native System
pages were exercised and captured directly.

Smoke tests copy saves into private test directories. The title/System runs
changed no character progression. No full-game traversal or all-cutscene
listening review is claimed; the previous retail AAC/FSB checks are recorded
in [0.7.3 validation](VALIDATION_0.7.3.md).

## Icons and documentation

Original SVG, 512×512 PNG and native BMP artwork are installed offline. The SDL
application identifier, desktop basename and StartupWMClass match
`cachyborne-play`; the new icon is visible on the actual KDE window. Optional
official artwork is fitted to a square canvas without distorting its ratio.
The icon and desktop caches were refreshed for the installed user.

47 Russian documentation files were removed. Guides for mods, upscaling and
roadmap were rewritten in English. Remaining project Markdown was checked for
Cyrillic text and local links; English/Spanish manuals link to the current
configuration, release notes and validation. Source releases exclude game data,
private profiles, saves, generated upscaler assets and local test captures.

Generated local evidence, excluded from release source:

- `out/validation-0.7.4-all.log`, `out/validation-0.7.4-headless.log`
- `out/validation-0.7.4-build.log`, `out/validation-0.7.4-setup.log`
- `out/validation-0.7.4-smoke.log`, `out/smoke-20261026-220843/game.log`
- `out/validation-0.7.4-title.png`, `out/validation-0.7.4-system-window.png`
- `out/validation-0.7.4-native-page.png`, `out/validation-0.7.4-native-page-smoke.log`
