# Current scope and remaining validation

Cachyborne 0.8.2 uses bbport 0.7 plus the reviewed community changes listed in
[the audit](COMMUNITY_AUDIT_0.8.2.md). The C runtime executes original guest code;
C++ translates GPU commands/shaders to Vulkan. Midnight owns native GTK4 settings,
prepared configurations, controls, mods, languages and diagnostics.

Release validation covers the runtime/renderer tests, real RADV operations,
FSR 4.1.2 replay at the target 1440p configuration and a bounded startup smoke.
It does not certify every scene, boss, mod or network peer combination.

Remaining work: full-mip writeback preserving padding and depth/stencil planes;
atomic ownership across concurrent guest writes; long lamp-travel/memory soaks;
review of the new effects-time accumulator against the existing frame hooks;
4K/ultrawide FSR differences; a defined nonblocking render-latency HUD; and a
separate interoperable port of Lantern room/co-op features. No placeholder
settings are exposed for these items.

See [architecture](../RESEARCH_AND_ARCHITECTURE.md), [GPU pipeline](parallel_gpu.md),
[upscaling](upscaler.md) and [validation](VALIDATION_0.8.2.md).
