# Cachyborne 0.8.3

Focused on the RX 9070 XT/RADV, i5-14600KF and 16 GB profile at 2560×1440.
The existing FSR 4.1.2 FP8 assets, 144 FPS cap, Mailbox, bounded direct-memory
configuration and P/E CPU affinity remain the installed profile.

- GPU-written linear images restore every resident mip, array layer and volume
  slice, retaining the guest offsets of reduced textures. Compact staging and
  valid-row writes preserve row, slice and mip padding. BC tails use block sizes.
- Depth transfers use the depth aspect's byte size; the independently addressed
  stencil target uses one byte per texel. Both guest allocations participate in
  the collector's reuse check. Unsupported tiled/MSAA/ambiguous stencil layouts
  remain resident rather than being discarded after an incomplete restoration.
- Packed rows are restored under one runtime VMA lock per slice, with a complete
  mapping preflight. Tight rows coalesce into a contiguous store. Buffer-cache
  invalidation occurs once per restored slice; disabled diagnostic recorders no
  longer require a function call for each row.
- Particle time accumulates complete 60 Hz steps at unlocked frame rates. The
  existing hook owns the clock; the game's zero-time flag is preserved and frames
  without a complete effect step skip that update. There is no shared code cave.
- A null online-event payload follows the game's unknown-event failure path,
  using an allocated native stub and an exact original-code signature check.

These are correctness changes and targeted reductions in allocation, locking
and transfer overhead, not a claim of a measured whole-game FPS increase.
See [source review](COMMUNITY_AUDIT_0.8.3.md), [validation](VALIDATION_0.8.3.md)
and [remaining scope](ROADMAP.md). No 4K/ultrawide work is included.
