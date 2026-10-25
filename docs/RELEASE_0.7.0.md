# Cachyborne 0.7.0 — Bisexual Midnight

A redesigned GTK4 / Libadwaita control center matching the BiMidnight desktop
palette: dark violet surfaces, lavender text and pink, purple and blue accents.
The new sidebar separates launch, hardware, graphics, controls and game setup.
High-contrast mode retains the native accessible appearance.

The recommended profile targets 144 FPS on an i5-14600KF, RX 9070 XT and 16 GB
RAM: 2560×1440 output, 1706×960 internal resolution, FSR 3.1 Quality, bounded
memory and two background pipeline compiler workers. Full RAM staging stays off.
Motion blur, chromatic aberration and depth of field are disabled by default.
This is a target configuration, not a guarantee of sustained gameplay frame rate.

The complete original bbport GTK editor remains available inside Cachyborne.
Its controller/keyboard bindings, mouse settings, effects, aspect ratios, display
and GPU selection, mod ordering, XML patches, FSR/DLSS asset tools, online/account
settings and diagnostics are imported into the prepared profile. Existing save
folders and custom bindings are retained. Changes take effect after setup.
Online configuration prepares local routing files without contacting a server;
connections happen when the user enables and starts online play.

Setup now carries the complete editor environment into patch compilation, retains
all game INI keys and exposes progress. The native renderer also handles the
original editor's FIFO Relaxed selection. Diagnostic logs work through the direct
launch path, MangoHud is not blocked by the RADV preset's default implicit-layer
filter, and dry-run output redacts online credentials.

## Install the source release

```bash
tar -xzf cachyborne-0.7.0-source.tar.gz
cd cachyborne-0.7.0
./cachyborne-install.sh
./cachyborne-setup.sh /path/to/CUSA03173 --performance-profile 144
./cachyborne-gui
```

The source asset includes the pinned submodule source needed by the build. The
installer installs CachyOS/Arch dependencies and compiles for the local CPU.
No game files, saves, account credentials, proprietary upscaler inputs or
machine-specific executable are included. A decrypted Bloodborne 1.09 dump is
required. Check the archive against SHA256SUMS before extracting.

## Validation

The final check passed 164 Python tests, 14/14 C++/Vulkan targets, the native C
regressions and SPIR-V validation. See the validation record in the source tree. GUI checks cover stylesheet parsing, page construction, the complete editor
and settings import. A local game setup validates the supplied dump and produces
the 144 FPS target profile. Startup verification is separate from a sustained
144 FPS traversal benchmark; HDR/VRR and long-session stability remain dependent
on the actual display, driver and game session.
