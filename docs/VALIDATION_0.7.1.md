> Historical validation record. See [0.7.4 results](VALIDATION_0.7.4.md) for the current changes and measurements.

# 0.7.1 validation record

CachyOS, Intel i5-14600KF, Radeon RX 9070 XT / RADV GFX1201, 16 GB RAM.
Native Clang, mold and ThinLTO build.

| Check | Result |
| --- | --- |
| `BB_BUILD_JOBS=2 bash scripts/check-all.sh` | Passed |
| Python suite | 174 tests passed |
| C++/Vulkan CTest | 14 of 14 passed |
| Native C regressions | Passed |
| Generated motion shader SPIR-V | Validated |
| Vulkan command submission and 4096-byte readback | Passed on RX 9070 XT |
| Native GTK construction, schema and save | All 41 original settings, 44 bindings, all 11 effects; one native window |
| Draft/default regression | Incomplete form saved; private personal defaults round trip |
| Setup regression | Real mod overlay and profile publication after successful scan, long and two-letter names |
| FSR 4 assets | 105 pinned v07 files downloaded locally for 2160 tier (also used for 1440p); preflight passed |
| FSR 4 provider execution | Native AA 2560×1440 to 2560×1440, 30 frames, exit 0 |
| Retail dump setup | 26,913 DCX files scanned, zero unpack failures; three selected mods combined successfully |
| User profile | FSR 4 Native AA, 144 FPS cap, 1440p, HDR, LOD 2 preserved as personal defaults |

The pre-existing GUI was left open; its current settings were saved and backed
up locally before migration. Newly opened windows use the migrated pages.
Network settings/sign-in are configuration tools; tests do not certify an
online multiplayer session. The provider test does not measure sustained game
FPS, HDR correctness, or a complete playthrough.
