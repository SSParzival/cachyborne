> Historical validation record. See [0.7.4 results](VALIDATION_0.7.4.md) for the current changes and measurements.

# 0.7.2: real-game startup investigation

Hardware: i5-14600KF, RX 9070 XT / RADV GFX1201, 16 GB RAM; Mesa 26.2.5.
AMD reports 8,250,814,464 bytes of GTT capacity (about 7.68 GiB).

## Reproduced failure

The real prepared game exited with code 23: GPU assertions reported
`ErrorDeviceLost`, and the kernel logged “Not enough memory for command
submission”. The native 2560×1440 scene enabled a 9152 MiB guest pool; the
initial allocations plus resource cache exceeded this machine's GTT budget.
Disabling preupload/HDR alone did not fix native startup. An experimental
VRAM-backed guest allocation did not provide a usable startup in this run.
Neither successful game preparation nor the standalone FSR benchmark had
exercised this workload. The earlier 0.7.1 record is not proof of game startup.

## Actual game execution

Testing used a copy of the user directory, without creating a test character
in the user's own saves. The game was exercised through the Spanish title
screen, initial settings, character creation, the opening 3D cinematic and
playable Iosefka's Clinic. GPU readbacks and window captures were visually
inspected. FSR 4 provider initialization was confirmed in the game log, including
its v07 INT8 Quality model and 2560×1440 output.

1916×1078 internal rendering keeps the 5056 MiB pool; it loaded successfully.
The renderer aligned the FSR input to 1916×1080. The menu, character screen and
parts of the opening cinematic reached 144 FPS after shader compilation. The
initial playable room measured approximately 135–138 FPS while stationary;
these are scene-specific observations, not a guarantee. Initial shader
compilation caused longer frames. Full-game traversal and all visual effects
have not been certified.

FSR 4 Quality at 1706×960 reached 144.0 FPS in repeated warmed intervals in
the first playable room and is the installed personal default, prioritizing
144 FPS over the custom higher internal size. HDR and full preupload are off.
The final installed Quality profile passed `python3 scripts/smoke-game.py
--seconds 60`: 60 seconds alive, visible GPU image, 11 FPS samples, last
sample 144.0 FPS. Local logs are under `out/launch-investigation/` and are
not included in the release. The test character remains only in isolated saves.

## Regression checks

- 188 Python tests and 14/14 C++/Vulkan CTest checks passed, plus native C tests,
  SPIR-V validation and Vulkan command submission/readback.
- New checks cover incomplete/missing language archives, Spanish defaults,
  Spanish regional fallback, unsupported language rejection, actual AMD GTT
  budgets, safe internal resolutions and alternate memory backends.
- Native GTK tests check that changing the preset updates internal resolution,
  and that its language selector contains only installed archive pairs.
- `scripts/smoke-game.py` starts the prepared retail game with isolated saves
  and requires an actual nonblank GPU readback and presented-frame statistics;
  it fails on early exit, device loss or missing rendering. It complements the
  synthetic suite and does not claim a complete playthrough.
