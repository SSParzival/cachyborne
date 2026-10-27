# Cachyborne status and remaining validation

The runtime executes the original x86-64 game code on the host CPU and provides
native system services, audio/input and a Vulkan renderer. The GTK4/Libadwaita
control center manages prepared generations, performance, bindings, content,
languages and diagnostics.

Current work includes independent Spanish text/layout and English speech,
Spanish port-owned in-game menus, robust headless CSS, case-insensitive Vulkan
presentation settings, a bounded 16 GB profile and application icons that match
the Wayland desktop identity. See [release notes](RELEASE_0.7.4.md).

Further validation should cover long gameplay traversals, boss cinematics,
additional language/dump combinations, 4K output within the safe internal-size
budget, different drivers/GPUs and interacting mods. Short startup checks and
synthetic tests do not certify these workloads or stable 144 FPS everywhere.

Architecture: [overview](../RESEARCH_AND_ARCHITECTURE.md),
[GPU pipeline](parallel_gpu.md), [upscaling](upscaler.md).
Reproducible evidence: [validation](VALIDATION_0.7.4.md).
