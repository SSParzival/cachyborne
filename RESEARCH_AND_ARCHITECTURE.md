# Cachyborne architecture

Cachyborne 0.8.3 targets Linux/CachyOS, a Radeon RX 9070 XT with RADV, an
Intel i5-14600KF and 16 GB of system RAM. The stable source base is bbport 0.7.

## Execution and preparation

The original decrypted x86-64 game code executes on the host CPU. The C runtime
implements guest system calls, threading, memory, input, audio, files, saves and
network services. `scripts/prepare.py` validates the 1.09 executable and content;
Midnight publishes completed generations atomically. Normal launch consumes that
prepared generation and performs no package installation or full asset scan.

## Memory and scheduling

`src/runtime_memory/` owns the sparse memfd, aliases, backing view, demand-committed
heaps and GPU imports. Reservations are not eagerly populated. The 16 GB profile
keeps game assets/caches on disk and rejects the large direct-memory configuration
needed for a native 1440p scene. `gpu/buffers/` and `gpu/layer/` track GPU-visible
ranges, mirrors and copyback. Driver capability checks and allocation fallbacks
remain necessary even with ReBAR.

`src/cachy_affinity.h` captures the original allowed CPU mask and intersects each
thread's requested placement with it. The target PC's actual topology is P CPUs
0–11, E CPUs 12–19. Main/simulation/render/audio remain on P CPUs; named shader
and decompression workers use E CPUs. PS4 logical affinity is a separate interface.
The upstream reservation monitor yields to this policy. No realtime privileges,
mlock of the guest pool or scheduler replacement is required.

## Rendering and caches

`gpu/cp/` decodes command packets; `gpu/render/` prepares draws, records commands,
publishes fences and manages pipelines; `gpu/shader/` recompiles guest shaders.
Persistent permutation indices cannot be reused, and compiler fingerprints protect
cache identities. Null SRT subtrees are zeroed by generated code; other invalid
pointers retain per-invocation fault recovery.

`gpu/textures/` manages Vulkan images and guest ownership. Marking, refresh and
collection compare the same byte range. A bounded streaming hash captures the
CPU baseline at GPU binding/copy/clear/resolve and refuses stale GC writeback.
Linear writeback restores all resident mips/layers/slices using compact staging and
valid-row transactions under a VMA lock. Depth and stencil use separate planes.
This hash and the VMA lock do not exclude raw concurrent guest CPU stores; tiled,
MSAA and ambiguous stencil layouts remain protected from incomplete GC restoration.

## Reconstruction, interface and build

FSR 4.1 replay dispatches translated shaders on Vulkan. The current asset helper
uses the official AMD SDK 2.4.0 / upscaler 4.1.2.4555 DLL only during extraction;
the game uses SPIR-V and model weights. FP8 matrices are selected from actual GPU
features and available models. FSR3 remains the fallback. Other GPU/Windows
backends retain their platform guards and are not assumed usable on RADV.

Midnight is Python/GTK4/Libadwaita. Its JSON preferences project into the shared
INI and runtime environment. Text, speech and menu locales are independent;
port-owned menus support English and Spanish. Sweet Purple SVGs are the icon
sources, with PNG/BMP exports for desktop and SDL consumers.

The native build uses Clang, native CPU instructions, ThinLTO and mold, with
bounded build jobs. FSR backend changes are stored as patches against a pinned
submodule and applied by `build.sh`, so fresh clones reproduce the source.

[Configuration](docs/CONFIGURATION.md) · [validation](docs/VALIDATION_0.8.3.md) ·
[community provenance](docs/COMMUNITY_AUDIT_0.8.3.md)
