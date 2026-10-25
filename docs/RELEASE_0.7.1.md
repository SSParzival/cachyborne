# Cachyborne 0.7.1

The original bbport configuration now lives entirely inside Midnight's native
GTK pages: controls/bindings, content ordering, XML patches, online/accounts,
display/GPU selection, upscaler tools and diagnostic options. The separate
complete-editor window is no longer used.

Fix setup failing with “too many values to unpack (expected 2)” after a clean
DCX scan when mods are enabled. The shared mod resolver now passes named layers
to the overlay builder. Setup regression tests cover long and two-letter names.

Draft saving no longer depends on a valid form or successful setup. Automatic
saving and personal default save/restore preserve edits, bindings and content
selection. FSR 4 is the default upscaler and asset preflight prevents accepting
missing/corrupt assets. Changing the FPS target preserves the user's selected
upscaler and quality. The 16 GB profile keeps full-game RAM staging disabled.

This is a source release with pinned submodules, excluding game data, saves,
credentials, downloaded upscaler assets and hardware-specific binaries. Build
locally; download FSR 4 assets before setup. See the
[validation record](VALIDATION_0.7.1.md). 144 FPS remains a target, not a guarantee.
