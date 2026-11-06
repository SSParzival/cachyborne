#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check the entire measured FSR 4.1 Native AA route, excluding loading/warmup.

Requires absolute monotonic frame timestamps. Relative logs cannot establish
which frames overlap a replay. Never selects a faster tail of the recording.
"""
import argparse
import csv
import io
import json
import math
from pathlib import Path
import re
from isolated_game import fault_checks

MAX_DURATION_SECONDS = 1800


def read_records(path):
    """The native writers terminate every complete CSV record with a newline."""
    data = Path(path).read_text()
    rows = list(csv.DictReader(io.StringIO(data)))
    for row in rows:
        row['_record_complete'] = True
    if rows and not data.endswith(('\n','\r')):
        rows[-1]['_record_complete'] = False
    return rows


def timed_window(rows, key, start, end, name):
    selected, previous, truncated = [], None, False
    for i,row in enumerate(rows):
        incomplete = row.get('_record_complete') is False
        if incomplete:
            if i == len(rows)-1 and previous is not None and previous > end:
                truncated = True
                break
            raise ValueError(f'Incomplete {name.lower()} record before measurement ended')
        try:
            stamp = int(row[key])
        except (TypeError,ValueError) as error:
            # Legacy in-memory callers have no newline metadata. Missing final
            # fields still need a complete timestamp strictly beyond the window.
            if (i == len(rows)-1 and row.get('_record_complete') is not True and
                row.get(key) in (None,'') and previous is not None and previous > end):
                truncated = True
                break
            raise ValueError(f'Incomplete {name.lower()} timestamp before measurement ended') from error
        if previous is not None and stamp <= previous:
            raise ValueError(f'{name} timestamps are not strictly monotonic')
        previous = stamp
        if start <= stamp <= end:
            selected.append(row)
    return selected, truncated


def analyze(frames, poses, log, run, saves_unchanged):
    start = int(run['replay_start_ns'])
    duration = float(run['duration_seconds'])
    target = int(run['fps'])
    if not 1 <= duration <= MAX_DURATION_SECONDS or not 1 <= target <= 1000:
        raise ValueError('Invalid measurement duration or frame-rate target')
    end = start + round(duration * 1e9)
    selected,truncated_tail = timed_window(frames,'monotonic_ns',start,end,'Frame')
    samples,truncated_poses = timed_window(poses,'ns',start,end,'Pose')
    times = [float(r['frame_ms']) for r in selected]
    if not times or any(not math.isfinite(t) or t <= 0 for t in times):
        raise ValueError('Missing or invalid measured frame times')
    ordered = sorted(times)
    p99 = ordered[max(0, math.ceil(len(ordered)*.99)-1)]
    fps = len(times) * 1000 / sum(times)
    intervals = [(int(b['monotonic_ns'])-int(a['monotonic_ns']))/1e6
                 for a,b in zip(selected,selected[1:])]
    interval_ordered = sorted(intervals or times)
    p99_interval = interval_ordered[max(0, math.ceil(len(interval_ordered)*.99)-1)]
    width,height = map(int,run['output_res'].split('x'))
    runtime_log = log.split('Acceptance: stopping isolated processes',1)[0]
    backend = re.search(r'Upscaler: FSR 4\.1 replay, (.+) output (\d+)x(\d+)',runtime_log)
    backend_native = bool(backend and tuple(map(int,backend.group(2,3))) == (width,height) and
        f'FSR 4 inputs {width}x{height} -> {width}x{height}' in runtime_log)
    fallback = bool(backend and 'falling back' in runtime_log[backend.end():])
    deadline = 1e9/target
    gates = {
        'native_configuration': run['render_res']==run['output_res']=='2560x1440' and
            run['upscaler']=='fsr411' and str(run['preset'])=='0' and
            run.get('frame_generation')=='off',
        'actual_fsr411_native_backend': backend_native and not fallback,
        'original_saves_unchanged': saves_unchanged is True,
        'complete_frame_window': len(times) >= duration*target*.95 and
            int(selected[0]['monotonic_ns'])-start <= 2*deadline and
            end-int(selected[-1]['monotonic_ns']) <= 2*deadline,
        'valid_gameplay_window': len(samples)>=duration*target*.9 and all(
            int(r['valid'])==1 and int(r['loading'])==0 and int(r['map'])!=0 and int(r['dropped'])==0
            for r in samples) and all(0<int(b['ns'])-int(a['ns'])<=100000000
                                     for a,b in zip(samples,samples[1:])),
        # Allow one boundary frame, but missing rows must not make a sparse
        # log look smooth solely because its remaining frame_ms values do.
        'average_fps': fps >= target*.985 and (len(times)+1)/duration >= target*.985,
        'p99_frame_time': p99 <= 1000/target*1.05,
        'worst_frame_time': max(times) <= 2000/target,
        'frame_timestamp_spacing': p99_interval <= 1000/target*1.05 and
            max(interval_ordered) <= 2000/target+1e-6,
        **fault_checks(log),
        'replay_finished': 'Runtime: pad replay finished' in runtime_log,
    }
    if run.get('expected_map') is not None:
        gates['requested_area'] = bool(samples) and all(int(r['map'])==int(run['expected_map']) for r in samples)
    return dict(passed=all(gates.values()),gates=gates,frames=len(times),pose_samples=len(samples),
                measured_fps=fps,p99_ms=p99,worst_ms=max(times),
                window_fps=len(times)/duration,p99_interval_ms=p99_interval,
                worst_interval_ms=max(interval_ordered),
                backend=backend.group(1) if backend else None,truncated_record_after_window=truncated_tail,
                truncated_pose_record_after_window=truncated_poses,
                limitation='One route only; heavy-area, soak, VRR, online and whole-port acceptance are separate.')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory',type=Path,help='Capture root containing manifest.json and <fps>/')
    parser.add_argument('--out',type=Path)
    args=parser.parse_args()
    try:
        manifest=json.loads((args.directory/'manifest.json').read_text())
        run=manifest['run']; directory=args.directory/str(run['fps'])
        frames=read_records(directory/'frames.csv')
        poses=read_records(directory/'poses.csv')
        result=analyze(frames,poses,(directory/'game.log').read_text(errors='replace'),run,
                       manifest.get('original_saves_unchanged'))
    except (OSError,KeyError,ValueError,TypeError,csv.Error) as error:
        result=dict(passed=False,error=str(error))
    text=json.dumps(result,indent=2)+'\n'
    if args.out: args.out.write_text(text)
    print(text,end='')
    return 0 if result['passed'] else 1


if __name__=='__main__':
    raise SystemExit(main())
