# 0.7.0 validation record

Environment: CachyOS, i5-14600KF, Radeon RX 9070 XT / RADV GFX1201,
16 GB installed system RAM. Native Clang + mold + ThinLTO build.

| Check | Result |
| --- | --- |
| `BB_BUILD_JOBS=2 bash scripts/check-all.sh` | Passed |
| Python regression suite | 164 tests passed |
| C++/Vulkan CTest suite | 14 of 14 passed |
| Native C regressions | Pad, runtime, file/mod access, semaphore, online imports, content and affinity passed |
| Generated motion shader SPIR-V validation | Passed |
| Vulkan submission and 4096-byte readback | Passed on the RX 9070 XT |
| Midnight stylesheet and native GTK rendering | Passed; native high-contrast appearance retained |
| GTK page construction and complete editor | Passed; 44 binding rows, 11 effects, settings import exercised |
| Original configuration coverage | All 41 settings defaults and 24 INI defaults retained, plus custom key/pad bindings |
| Setup of the supplied retail dump | Executable accepted; 0 failures among 26,913 DCX files |
| Prepared target profile | 144 FPS cap, FSR 3.1 Quality, 1706×960 internal / 2560×1440 output, 16 GB memory limits |
| Extracted source archive | CLI/shell checks, CSS and pinned submodule presence, reverse FSR patch check passed |
| Source archive checksum | SHA-256 verified |
| Local Markdown links and `git diff --check` | Passed |

The complete editor's network/account settings are tested as local configuration.
Tests do not contact a community server or certify a multiplayer session. The
source archive contains neither the supplied game dump nor saves/credentials.

The 144 FPS profile is an optimization target. This record does not establish
sustained 144 FPS in Central Yharnam, certify HDR/VRR, or prove a full playthrough.
The user previously confirmed successful game execution; the new profile is
validated and installed locally for the next launch. Source/native dependency
warnings from upstream and the current RADV driver are not treated as a claim
of a warning-free toolchain.
