#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Measure the published 1440p FSR 4.1 Native AA profile on copied offline saves.

Requires 8 GiB available for 20 continuous seconds before launching. Each run
retains its full frame window, binary hashes and save-integrity evidence.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import time

from capture_physics import capture, core, hashes
from native_acceptance import MAX_DURATION_SECONDS, analyze, read_records
from param_tables import load_table


def requested_area(game, lamp, expected_map):
    if lamp is None:
        return None
    rows = [row for key, row in load_table(game, 'ReturnPointParam') if key == lamp]
    if len(rows) != 1:
        raise ValueError('Area lamp is absent or ambiguous in ReturnPointParam')
    area, block = rows[0]['areaNo'], rows[0]['blockNo']
    if not 0 <= area <= 255 or not 0 <= block <= 255 or expected_map != (area << 24 | block << 16):
        raise ValueError('Requested map differs from the lamp in ReturnPointParam')
    names = {(21, 0): "Hunter's Dream", (23, 0): 'Old Yharnam',
             (24, 0): 'Cathedral Ward', (24, 1): 'Central Yharnam',
             (25, 0): 'Cainhurst', (26, 0): 'Nightmare of Mensis',
             (27, 0): 'Forbidden Woods', (32, 0): 'Byrgenwerth'}
    return dict(lamp=lamp, map=expected_map, block=f'm{area:02}_{block:02}_00_00',
                name=names.get((area, block)))


def binary_hashes():
    return {str(path): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in (core.ROOT / 'out/bb-probe', core.ROOT / 'out/gpu/libbbgpu.so')}


def target_rate_gate(result, duration):
    # The general analyzer permits a 1.5% performance margin. A published
    # 60-FPS verification must also meet the actual rate, not just its cap.
    return (abs(result.get('measured_fps',float('nan'))-60) <= .1 and
            abs(result.get('window_fps',float('nan'))-60) <= max(.1,1/duration))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile', type=Path)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--duration', type=int, default=120)
    parser.add_argument('--warmup', type=int, default=30)
    parser.add_argument('--replay', type=Path, help='Optional recorded pad route; copied and hashed before launch')
    parser.add_argument('--admission-timeout', type=int, default=600)
    parser.add_argument('--warp-lamp', type=int)
    parser.add_argument('--expected-map', type=lambda value: int(value, 0))
    parser.add_argument('--host-trace', action='store_true', help='Record host resources; adds sampling overhead')
    parser.add_argument('--desktop', action='store_true', help='Show the copied-save test on the desktop with real audio')
    parser.add_argument('--monitor', help='SDL display number/name; requires --desktop')
    args = parser.parse_args(argv)
    if not 1 <= args.duration <= MAX_DURATION_SECONDS or not 0 <= args.warmup <= 120 or args.admission_timeout < 20:
        parser.error('duration 1..1800, warmup 0..120 and admission-timeout >=20 required')
    if (args.warp_lamp is None) != (args.expected_map is None):
        parser.error('--warp-lamp and --expected-map must be supplied together')
    if args.monitor is not None and not args.desktop:
        parser.error('--monitor requires --desktop')
    profile = core.load_profile(args.profile)
    core.check_runtime_requirements(profile)
    out = args.out.resolve()
    out.mkdir(mode=0o700, parents=True, exist_ok=False)
    manifest = dict(original_saves_unchanged=False, binary_unchanged=False,
                    admission=dict(required_gib=8, stable_seconds=20, admitted=False))
    manifest['harness_sha256'] = {name: hashlib.sha256((Path(__file__).parent/name).read_bytes()).hexdigest()
                                 for name in ('native_acceptance.py','capture_physics.py','isolated_game.py',
                                              'verify_native.py','param_tables.py','world_assets.py')}
    original = hashes(Path(profile['user']) / 'savedata')
    identity = binary_hashes()
    result = dict(passed=False)
    try:
        manifest['requested_area'] = requested_area(profile['game'], args.warp_lamp, args.expected_map)
        if manifest['requested_area']:
            print('Requested area: ' + json.dumps(manifest['requested_area']), flush=True)
        start = time.monotonic()
        stable = None
        last_report = start - 30
        while time.monotonic() - start < args.admission_timeout:
            now = time.monotonic()
            available = core.available_memory()
            stable = (stable if stable is not None else now) if available >= 8 << 30 else None
            if now - last_report >= 30:
                print(f'Admission: {available / 2**30:.2f} GiB available; waiting for 8 GiB stable for 20 s', flush=True)
                last_report = now
            if stable is not None and now - stable >= 20:
                manifest['admission']['admitted'] = True
                break
            time.sleep(1)
        else:
            raise RuntimeError('Memory admission expired; game was not launched')
        if binary_hashes() != identity:
            raise RuntimeError('Binaries changed while waiting for admission')
        replay = out / 'walk.pad'
        if args.replay is None:
            replay.write_text(f'0 0 128 0 128 128 0 0\n{args.duration * 1000} 0 128 128 128 128 0 0\n')
        else:
            replay.write_bytes(args.replay.read_bytes())
        manifest['replay_sha256'] = hashlib.sha256(replay.read_bytes()).hexdigest()
        manifest['run'] = capture(profile, 60, replay, args.duration, out / '60',
                                  warmup=args.warmup, require_backend='Upscaler: FSR 4.1 replay, FP8',
                                  audio=True, audio_dump=False, host_trace=args.host_trace,
                                  warp_lamp=args.warp_lamp, expected_map=args.expected_map,
                                  presentation='desktop' if args.desktop else 'headless',monitor=args.monitor)
        frames = read_records(out / '60/frames.csv')
        poses = read_records(out / '60/poses.csv')
        result = analyze(frames, poses, (out / '60/game.log').read_text(errors='replace'),
                         manifest['run'], original == hashes(Path(profile['user']) / 'savedata'))
        result['gates']['target_60fps'] = target_rate_gate(result,args.duration)
        result['target_rate_tolerance_fps'] = .1
    except (OSError, RuntimeError, ValueError, TypeError, KeyError, csv.Error) as error:
        result = dict(passed=False, error=str(error))
    finally:
        manifest['binary_sha256'] = identity
        manifest['binary_unchanged'] = binary_hashes() == identity
        manifest['original_saves_unchanged'] = original == hashes(Path(profile['user']) / 'savedata')
        result.setdefault('gates', {}).update(binary_unchanged=manifest['binary_unchanged'],
                                             original_saves_unchanged=manifest['original_saves_unchanged'])
        result['passed'] = result['passed'] and all(result['gates'].values())
        (out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
        (out / 'acceptance.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
