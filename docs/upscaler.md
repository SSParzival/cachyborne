# Upscaling on the target Radeon/RADV PC

The default is **FSR 4.1 replay with AMD 4.1.2.4555 assets**, SDK 2.4.0, Quality,
1706×960 scene → 2560×1440 output. The internal INI key remains `fsr411` for
configuration compatibility. `fsr4_model=auto` selects native FP8 matrices on
this RX 9070 XT when supported; it falls back according to device capabilities
and the models actually present. A missing/invalid asset set causes an explicit
FSR3 fallback during preparation.

## Install and select current assets

```sh
bash tools/fetch_fsr412_assets.sh
cachyborne-setup /path/to/decrypted/app0 --performance-profile 144
cachyborne-launch --dry-run
```

The helper retrieves the signed upscaler from the official AMD SDK v2.4.0 tag,
checks a pinned SHA-256, validates its model/exports, records dispatches through
Proton and translates them with the pinned dxil-spirv tool. DLLs and generated
assets remain local and are never distributed by this repository. The game runs
the reconstructed model directly on Vulkan; the Windows SDK DLL is not loaded
into the native Linux game process.

Fresh extraction requires the dependencies described by
`tools/fsr4cap/build_assets.sh`, an installed usable Proton build and its Steam
runtime (or umu). Existing tools can be reused with `BB_FSR4CAP_TOOLS`.
Assets default to `$XDG_DATA_HOME/cachyborne/fsr4_412`; select another directory
in Midnight's FSR assets field. The 4.1.x DLL and optional loader fields are for
manual extraction, not libraries loaded at game startup.

## Validation and limits

The 0.8.2 synthetic FP8 comparison is byte-identical to the SDK DLL at
2560×1440 with both 1706×960 and aligned 1708×960 input over 30 frames, as well
as 1707×960 over three frames and several other tested sizes. The pass
cost in three alternating pairs at 1706×960 → 2560×1440 is about 0.89 ms for both
the previous assets and SDK 2.4 assets: no FPS gain is inferred from the update.
Two tested combinations (2259×1271 → 3840×2160 and 1707×720 → 2560×1080) differ
from the DLL. These modes are experimental; the complete replay is not certified
bit-exact at every size. See [release validation](VALIDATION_0.8.2.md).

FSR scene samplers use `log2(render/output)-1`; other passes retain their bias.
This changes texture sampling detail/bandwidth, not image allocation budgets.
Camera cuts reset temporal history; motion vectors are sanitized before entering it. Camera input,
object identity, disocclusion and exposure can still affect ghosting; sharpening
cannot repair missing geometry or invalid motion. Frame generation is disabled
by default and its display rate is not the simulation FPS.

## Other choices

FSR3 is the native fallback. FSR1 is spatial and cheaper; TAA performs temporal
anti-aliasing at output resolution; DLSS requires its supported NVIDIA hardware
and libraries. The older INT8 v07 provider remains optional via
`tools/fetch_fsr4_assets.sh`; it is not the SDK 4.1.2 model.

At 4K, a 1920×1080 internal scene corresponds to Performance, not Balanced. The
16 GB profile limits internal size independently of output size. Native AA at
1440p/4K still runs reconstruction and may require an unsafe game heap size.
Do not infer safety or stable 144 FPS from the name of a preset.
