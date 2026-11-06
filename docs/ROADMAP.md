# Current scope and remaining validation

Cachyborne 0.8.3 uses bbport 0.7 plus the reviewed changes in the
[0.8.2 inventory](COMMUNITY_AUDIT_0.8.2.md) and
[0.8.3 source review](COMMUNITY_AUDIT_0.8.3.md). The C runtime executes original
guest code; C++ translates GPU commands/shaders to Vulkan. Midnight owns native
GTK4 settings, prepared configurations, controls, mods, languages and diagnostics.

Implemented in 0.8.3: full resident-mip linear writeback with padding preservation,
depth/stencil aspect sizes, compact staging, per-slice VMA transactions, particle
60 Hz accumulation integrated into the existing frame hook, and a native null-payload
guard for online events checked against the exact original instruction bytes.

The current target is 2560×1440 input and output, FSR 4.1 Native AA (preset 0,
1x), 60 FPS without frame generation, on this PC's RX 9070 XT/RADV and
i5-14600KF with P/E affinity. See [native verification](VERIFY_NATIVE.md) for
the measured-window requirements. Historical 144 FPS tests do not qualify this
profile. 4K/ultrawide investigation and tuning are outside scope.

Remaining engineering/validation boundaries:

- Atomic ownership against raw concurrent guest CPU writes and heap reuse.
  Per-slice VMA locking excludes remapping during a copy; hashing detects many
  reuses, but neither is a complete guest-allocation ownership protocol.
- Tiled/MSAA collection writeback and ambiguous multi-mip stencil layouts.
  Keep those GPU-written images instead of evicting after an incomplete download.
- Repeated lamp-travel/GC pressure soaks and targeted boss particle/physics
  comparisons. A bounded scene run is not a full-session leak or boss test.
- A render-latency HUD with a precisely defined, nonblocking measured interval.
- A separate interoperable port of Lantern room/co-op contracts and peer tests.

No placeholder settings are exposed for these items. See
[architecture](../RESEARCH_AND_ARCHITECTURE.md), [GPU pipeline](parallel_gpu.md),
[upscaling](upscaler.md) and [validation](VALIDATION_0.8.3.md).
