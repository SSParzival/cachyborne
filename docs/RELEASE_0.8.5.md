# Cachyborne 0.8.5

Bugfix release resolving hardware capabilities probing and setup failures when preparing without pre-built artifacts, alongside full native binary build verification.

## Highlights & Fixes

- **Automatic Hardware Capabilities Tool Provisioning**:
  - `launcher/cachyborne_options.py` now provides `ensure_capabilities_tool()`. If `out/bb-gpu-capabilities` does not exist when settings or game setup validate hardware parameters, it is automatically built on demand from `tools/gpu_capabilities.c` using the native C compiler and Vulkan/SDL3 libraries.
  - Prevents `FileNotFoundError: [Errno 2] No such file or directory: 'out/bb-gpu-capabilities'` from crashing `cachyborne-setup.sh` or GUI profile preparation.
- **Robust Display & Resolution Probing**:
  - `capabilities(root, mode)` in `launcher/cachyborne_options.py` now gracefully handles missing video devices, headless environments, and sandbox restrictions without throwing unhandled exceptions.
  - `monitor_output(root, selected)` falls back cleanly to the configured resolution from user settings (`settings.json`) when displays cannot be enumerated.
  - Guarded live resolution detection (`live == 'auto'`) in `launcher/cachyborne.py` with error fallback.
- **Native Dependency Prefix Support**:
  - Integrated local prefix support (`out/native-deps/usr`) enabling complete builds via `build.sh` on systems without root permissions.
  - Verified full build and execution of `out/bb-probe`, `out/gpu/libbbgpu.so`, `out/gpu/libbbnet20.so`, and `out/gpu/rebirth-script-tool`.
