# Cachyborne 0.8.2

This release publishes the bbport 0.7 integration, selected community fixes and
native Midnight configuration work together with the follow-up review on the
RX 9070 XT/RADV, i5-14600KF and 16 GB target.

- The texture collector compares the same full guest byte range at marking,
  refresh and collection. GPU bindings, copies, clears and resolves establish a
  bounded streaming ownership hash; unknown/changed ownership rejects stale
  collector writeback. No second full host image is retained.
- Null resource-table subtrees are zeroed by generated SRT code. Invalid non-null
  pointers retain per-invocation fault recovery. Camera matrices/motion vectors
  reject non-finite values and bound extreme vectors before temporal history.
- FSR scene-material mip bias restores the detail expected by reconstruction;
  camera jumps reset temporal history. These are scoped Pera ports with CPU
  formula tests, keeping non-FSR sampler behavior and the 16 GB allocation policy.
- Existing shader permutation reservations, coherent mip bindings, deterministic
  phi processing, compiler fingerprints, serialization bounds and BC6H fallback
  were retained and tested. The P/E affinity policy and bounded memory profile
  remain authoritative.
- Official AMD SDK 2.4.0 / upscaler 4.1.2.4555 can be downloaded with a pinned
  checksum and extracted to native Vulkan assets. The target model is FP8 when
  advertised by the GPU. Configuration defaults use the new local asset folder;
  the stable internal `fsr411` key and FSR3 fallback remain.
- Port-owned runtime menus support English and Spanish. Reference launcher
  catalogs now use English keys; shared schema translations and tests were
  migrated together. Speech/game locale IDs remain separate compatibility data.
- Original Sweet Purple SVG/PNG game/settings icons and the SDL BMP replace the
  old artwork. The installer registers both desktop identities offline.
- Documentation is consolidated around the current release, configuration,
  architecture, community provenance and reproducible validation.

The integration also includes the upstream 0.7 split memory/input/GPU modules,
local trophies, controller/menu improvements, FSR1, SMAA/FXAA, GT-VBAO,
low-latency backends and optional frame-generation/online components. Platform
capabilities remain checked; Windows DLL backends are not assumed native to Linux.

A hash does not provide atomic exclusion against concurrent guest writes.
Full-mip writeback, long traversal/physics testing, a measured latency HUD and
Lantern room interoperability remain separate work. Two tested 4K/ultrawide FSR
cases differ from the DLL; no claim of perfect reconstruction or stable 144 FPS
in every scene is made. See [validation](VALIDATION_0.8.2.md),
[audit](COMMUNITY_AUDIT_0.8.2.md) and [remaining scope](ROADMAP.md).

FSR replay builders are now named `bb:FsrBuild` and classified as background
compilation workers by the existing affinity policy. They can move from their
creator's P-Core mask to the permitted E-Core mask; the real-affinity regression
covers this role. Rendering, audio and simulation placement remain unchanged.
