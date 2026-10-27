# Temporal upscaling in Cachyborne

The renderer reconstructs the scene before composing the interface. The native
control center and in-game menu expose output size, internal scene size, preset,
RCAS sharpness and effect controls. Setup prepares the corresponding resolution
patches. Fixed-resolution sessions need preparation/restart for size changes;
live resolution is a separate option with a different post-processing cost.

## Providers

| Provider | Inputs and requirements |
| --- | --- |
| Off | Original scene without temporal reconstruction |
| TAA | Native-resolution temporal anti-aliasing with scene jitter, motion vectors, depth rejection and history |
| FSR 3.1 | Open-source FSR-Vulkan provider; fallback when FSR 4 assets are missing |
| FSR 4 | v07 INT8 model, external precompiled shaders, weights and matching manifest |
| FSR 4.1.1 | Separate replay assets prepared from the user's AMD DLL |
| DLSS | Supported NVIDIA hardware, the bridge and matching Linux library |

FSR and TAA keep the HUD out of their scene history. Object motion vectors
improve animated clothing/weapons; camera vectors alone cannot describe every
moving surface. Reactive-mask controls can reduce trails around transparent
effects. They are diagnostic/quality tradeoffs, not universal performance fixes.

## Safe presets for the 16 GB target

The recommended profile is FSR 4 Quality: **1706×960 → 2560×1440**, capped at
144 FPS. Missing/invalid FSR 4 assets produce a visible warning and setup
publishes an FSR 3.1 profile instead. The active prepared INI and environment
can be inspected with `cachyborne-launch --dry-run`.

For output 3840×2160, a 1920×1080 scene has the 2× **Performance** ratio.
Balanced at 4K requires a larger scene and is not equivalent to 1080p.
The 16 GB profile rejects internal sizes above 1920×1080 because they need the
9152 MiB direct-memory pool. FSR Native AA renders at output size and still runs
the reconstruction model; it is not a cheap pass-through.

Install optional FSR 4 assets with:

```bash
bash tools/fetch_fsr4_assets.sh
```

The selected set must contain RCAS, auto exposure, model weights, initializers,
manifest and the matching pre/pass/post SPIR-V shaders. Setup checks presence,
nonempty files, SPIR-V headers and the JSON manifest. Game preparation still
performs its own executable and DCX checks.

FSR 4.1.1 assets are separate:

```bash
bash tools/fsr4cap/build_assets.sh /path/to/amd_fidelityfx_upscaler.dll
```

Follow that tool's dependency checks and optional loader argument. Proprietary
inputs and generated model assets are not part of the source release. FSR 4.1.1
selection validates its assets independently; the FSR 4 v07 fallback does not
silently replace an explicitly selected DLL-based provider.

## Measurement and limitations

Shader compilation, history resets, provider/preset changes and asset streaming
can cause long frames. Test both cold and warmed intervals in actual gameplay;
a standalone upscaler benchmark does not prove the game fits RAM/GTT. Disabling
motion blur, chromatic aberration and depth of field is part of the recommended
profile. GameMode, frame generation and driver overrides should be evaluated
separately. Frame generation does not increase simulation or input sampling
speed and remains an advanced diagnostic option.

See [configuration](CONFIGURATION.md), [GPU pipeline notes](parallel_gpu.md),
and [current validation](VALIDATION_0.7.4.md).
