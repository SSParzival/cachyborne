# Cachyborne 0.7.3

English speech and Spanish text are independently configurable. Retail FSB banks
redirect to an available selected-language alternative; AAC cutscene selection
uses the same language policy for automatic and explicit stream selection.
The native GUI exposes a voice selector beside the installed text languages.

The recommended preset now applies 1706×960 → 2560×1440 Quality, a 144 FPS cap,
P/E CPU lists, Mailbox, English voices and Spanish text. Costly blur, chromatic
aberration and depth of field are disabled. Existing bindings, paths, HDR and
content selection survive the preset. Missing FSR 4 assets fall back to FSR 3.1.
The 16 GB memory profile rejects the 9152 MiB native-1440p pool even without GTT
telemetry. Plans explicitly request 3 Vulkan recording threads and async submit.

GTK CSS callbacks tolerate an absent display. Vulkan present-mode parsing is
case insensitive. Python syntax checks use an in-memory AST and disable bytecode
writes during the regression suite. Guest paths exceeding the translation buffer
are rejected instead of silently truncated.

[Spanish manual](../README.es.md) · [Configuration](CONFIGURATION.md) ·
[Measurements and test results](VALIDATION_0.7.3.md)
