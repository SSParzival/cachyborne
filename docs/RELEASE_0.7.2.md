# Cachyborne 0.7.2

Fix accepting a native 1440p profile that exceeds AMD's GTT capacity on the
16 GB target system, causing the game window to close with device loss. Setup
now checks the memory budget, and newly generated direct launch plans refuse
an insufficient budget with an actionable message. Lower internal rendering
with 1440p FSR 4 output keeps the smaller guest pool.

Changing the upscaler preset now recalculates internal resolution. This fixes
Quality being selected while a previous native scene resolution stayed active.
Explicit custom internal sizes remain editable and are preserved on reload.

Spanish is the setup default, preferring the installed Latin American variant.
The GTK language list is derived from complete message-archive pairs in the
selected dump. Unsupported selections are rejected before expensive setup.

Add a real-game startup check with isolated saves, GPU image readback and frame
statistics. Testing now includes the actual title screen, character creation,
opening cinematic and first playable room. 144 FPS is the cap/target; measured
performance varies by scene and shader-cache state. See the
[validation record](VALIDATION_0.7.2.md).

Source release only; game assets, personal configuration and test saves are excluded.
