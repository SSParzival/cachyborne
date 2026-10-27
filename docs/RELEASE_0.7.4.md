# Cachyborne 0.7.4 — Spanish menus, English voices and native icons

Previously `BB_MENU_LANGUAGE=es` was exported by the launcher, but the runtime
only understood English/Russian and guessed every Latin-script game label was
English. An older prepared profile could also retain British English text.
The runtime now provides Spanish graphics-overlay and native System-page
catalogs. Explicit launcher locale overrides a stale saved INI language.

Text, localized layout and font requests follow `BB_LANGUAGE=20`, with Spanish
Spain / English fallback when an asset is absent. Voice banks and AAC tracks
follow `BB_VOICE_LANGUAGE=eng` independently. Existing users must run setup again
to regenerate the prepared execution plan; new defaults are Spanish text/menu
and English speech. Game files are not rewritten.

Original square SVG/PNG/BMP artwork replaces the generic application icon.
The desktop entry and SDL window share `cachyborne-play` identity for KDE/Wayland;
X11 receives a native window icon as well. The GUI contains the artwork without
stretching. Optional official-art downloads preserve aspect ratio, and default
installation no longer requires a network request for artwork.

Russian project documentation has been removed, including historical documents
without a `.ru.md` suffix. The mods, upscaling and roadmap guides are now English;
Spanish and English top-level manuals remain. Runtime Russian translations remain
available as a UI locale.

This release also includes the pending 0.7.3 robustness/performance changes:
headless GTK CSS, case-insensitive present modes, AST-based Python syntax checks,
FSR 3.1 asset fallback, the safe 16 GB profile and explicit Vulkan worker settings.

[Validation](VALIDATION_0.7.4.md) · [Spanish manual](../README.es.md) ·
[Configuration](CONFIGURATION.md)
