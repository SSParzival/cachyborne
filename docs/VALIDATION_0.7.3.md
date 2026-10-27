# Cachyborne 0.7.3 — validation, 2026-10-26

Tested locally on the i5-14600KF / RX 9070 XT (RADV GFX1201), Mesa
26.2.5-arch3.1, Vulkan 1.4.354 and 16 GB RAM. These results describe the tested
startup and regression cases; they do not certify all gameplay or stable 144 FPS
throughout the game.

## Required commands

| Check | Result |
| --- | --- |
| `bash build.sh --test` | Exit 0; C/C++, GPU library/shaders and native C tests passed |
| `bash scripts/check-all.sh` | Exit 0; 14/14 CTest checks, 192 Python tests, SPIR-V validation and device-backed Vulkan probe passed |
| `python3 scripts/smoke-game.py --seconds 15` | Exit 0 on the existing prepared profile; visible GPU content, one FPS sample at 137.5 |
| Smoke on a newly prepared isolated recommended profile | Exit 0; 15 seconds alive, visible GPU content, two samples: 138.7 and 144.0 FPS |
| Headless `test_midnight_settings.py` | 10 tests, no failures/errors; one display-dependent window test skipped |
| Normal display-dependent native GUI test | Passed as part of the complete 192-test suite |

The final smoke invocation used:

```bash
BB_PRESENT_MODE=mailbox BB_FILE_TRACE=1 BB_AUDIO_TRACE=1 \
  python3 scripts/smoke-game.py --seconds 15 \
  --profile out/validation-0.7.3-profile/config/game_profile.json
```

That profile was built by `core.setup()` under an isolated `out/` configuration,
data, cache and user directory using the recommended 1706×960 → 2560×1440 FSR 4
Quality preset. The user's published profile and saves were not replaced. The
smoke runner also copied its user directory before executing the game.

## Observed runtime evidence

- RADV identified the RX 9070 XT / GFX1201 and used three Vulkan recording
  threads with asynchronous submission and the layer memory module.
- The initialization CPU-read microbenchmark through the driver's dma-buf
  reported **86,290 MB/s**. This exceeds 30 GB/s in that specific measurement;
  it is not a guarantee of sustained end-to-end game bandwidth.
- Spanish `msg/spaes/item.msgbnd.dcx` and `menu.msgbnd.dcx`, and Spanish menu
  layout/font paths were opened. The plan exported text ID `20`; this retail
  startup resolved Spanish through the Spain archives. Both Spanish archive
  variants are present; the observed startup is evidence of Spanish loading,
  not proof that this game build maps ID 20 to `spaar` in every context.
- The real game logged four redirects from `sprj_main_ese.fsb` to
  `sprj_main_eng.fsb`. A separate native test opened **all 68** `_esa` / `_ese`
  banks through the actual runtime translator and verified English FSB5 headers.
- Lowercase `BB_PRESENT_MODE=mailbox` produced no unknown-mode diagnostic.
- Initial shader compilation was visible in the 138.7 FPS interval; the final
  warmed interval reached 144.0 FPS, with a reported worst frame of 7.8 ms.
  A short title/startup run cannot establish stable 144 FPS in combat or 4K.

## Cutscene finding and fix

FFprobe found eight AAC tracks in the 99,240,674-byte retail
`dvdroot_ps4/movie/sprj_opening.mp4`, all tagged `und`. Selecting by metadata
alone would not honor the voice preference. Language identification on decoded
PCM located Japanese on audio ordinal 0 and English on ordinal 1 (FFmpeg stream
2). The fallback table is limited to this filename, byte size and the expected
one-video/eight-AAC layout; explicitly tagged and differently structured videos
retain their own metadata.

A native check against `BbAvPlayer::Source` opened the actual MP4 using file
callbacks, enumerated all nine streams and verified the assigned voice codes.
Player selection uses the same preference for automatic startup and explicit
guest audio selections. This check validates stream assignment; the 15-second
startup, which skips the intro, does not constitute an audiovisual review of the
entire opening cutscene. PCM analysis used a temporary validation dependency;
no speech-recognition package is needed by the runtime or regression suite.

## Additional checks and limits

The recommended preset retains custom bindings and content while resetting
unsafe internal resolutions, expensive effects and language choices. Tests
cover FSR 4 → FSR 3.1 preparation, persistence of all supported voice codes,
missing displays including high-contrast callbacks, unavailable text archives,
missing voice banks and 16 GB rejection even without readable GTT telemetry.
The official desktop artwork was downloaded and verified as a 512×512 PNG.

The Python syntax check parses bytes to an AST in memory; bytecode generation
is disabled for the remaining Python suite. A complete build still requires a
writable `out/` tree. RADV emitted its existing nonconformance/testing warning;
Vulkan submission/readback passed. This run did not validate another Mesa
version, another GPU, every mod, every cutscene or a full game traversal.

Local evidence (generated, not shipped):

- `out/validation-0.7.3-build.log`, `out/validation-0.7.3-all.log`
- `out/validation-0.7.3-headless.log`, `out/validation-0.7.3-setup.log`
- `out/validation-0.7.3-retail-voices.log`, `out/validation-0.7.3-retail-movie.log`
- `out/validation-0.7.3-movie-audio.json`, `out/validation-0.7.3-track-analysis.log`
- `out/validation-0.7.3-smoke-final.log`, `out/smoke-20261026-213721/game.log`
