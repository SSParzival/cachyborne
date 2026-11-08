# Cachyborne 0.8.6

Small in-game presentation fix.

## Fixes

- **No shader compilation box during play**: the "Compiling shaders (N)" / "Compilando shaders (N)"
  box in the bottom-left corner is no longer drawn by default. Background pipeline creation and the
  pipeline cache are unchanged. Set `BB_SHADER_PROGRESS=1` to show it again.

## Verification

- `pytest -v`: 325 passed, 15 skipped, 1469 subtests passed.
- `build.sh`: `out/gpu/libbbgpu.so`, `out/gpu/libbbnet20.so` and `out/bb-probe` built (LTO on).
- Headless retail runs (`scripts/smoke-game.py`, gamescope headless, no audio, copied saves) with the
  pipeline cache off to force 62 runtime compiles: with `BB_SHADER_PROGRESS=1` the overlay logs
  `"Compiling shaders" shown (25 just now)`; with the default it is never shown. Both runs presented
  frames at 60 FPS.
