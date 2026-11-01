#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare captured guest trajectories; this does not certify unobserved Havok state.

Manifest format: {"runs": [{"fps":30,"poses":"30/poses.csv",
"replay_start_ns":123,"duration_seconds":8}, ...]}. Paths are relative to
manifest. All runs must use the same copied save, map, inputs and render settings.
Replay anchors come from the runtime, not process startup or loading duration.
"""
import argparse
import bisect
import csv
import json
import math
from pathlib import Path


def trajectory(path, start, duration):
    points=[]
    with Path(path).open() as file:
        for row in csv.DictReader(file):
            ns=int(row['ns']); t=(ns-start)/1e9
            if int(row['dropped']): raise ValueError('Dropped telemetry makes the trace incomplete')
            if not -0.1 <= t <= duration+0.1: continue
            if row['valid']!='1' or row['loading']!='0':
                raise ValueError('Invalid or loading sample inside the replay window')
            point=tuple(float(row[key]) for key in ('x','y','z'))
            if not all(map(math.isfinite,point)): raise ValueError('Nonfinite trajectory')
            points.append((t,int(row['map']),point))
    if len(points)<3 or points[0][0]>0 or points[-1][0]<duration:
        raise ValueError('Trace does not cover the replay interval')
    if len({p[1] for p in points})!=1: raise ValueError('Map changed during replay')
    times=[p[0] for p in points]
    if any(b<=a or b-a>0.1 for a,b in zip(times,times[1:])):
        raise ValueError('Nonmonotonic timestamps or sampling gap over 100 ms')
    return points


def at(points,t):
    index=bisect.bisect_right([p[0] for p in points],t)
    if index==0: return points[0][2]
    if index==len(points): return points[-1][2]
    a,b=points[index-1],points[index]
    fraction=(t-a[0])/(b[0]-a[0])
    return tuple(x+(y-x)*fraction for x,y in zip(a[2],b[2]))


def compare(baseline,candidate,duration):
    if baseline[0][1]!=candidate[0][1]: raise ValueError('Different maps')
    origin=at(baseline,0); other=at(candidate,0)
    offset=math.dist(origin,other)
    if offset>0.001: raise ValueError('Initial positions differ by over 1 mm')
    times=[i/30 for i in range(int(duration*30)+1)]
    reference=[at(baseline,t) for t in times]
    observed=[at(candidate,t) for t in times]
    horizontal=max(math.hypot(p[0]-origin[0],p[2]-origin[2]) for p in reference)
    if horizontal<0.1: raise ValueError('Stationary trace cannot certify jump/movement equivalence')
    max_horizontal=max(math.hypot(a[0]-b[0],a[2]-b[2]) for a,b in zip(reference,observed))
    vertical=max(abs(a[1]-b[1]) for a,b in zip(reference,observed))
    return dict(initial_offset_m=offset,baseline_horizontal_extent_m=horizontal,
                max_horizontal_error_m=max_horizontal,horizontal_error_fraction=max_horizontal/horizontal,
                max_vertical_error_m=vertical,horizontal_within_0_1_percent=max_horizontal/horizontal<0.001)


def evaluate(manifest,base):
    runs=manifest['runs']; rates=[r['fps'] for r in runs]
    if sorted(rates)!=[30,60,144]: raise ValueError('Exactly one 30, 60 and 144 FPS capture required')
    durations={r['duration_seconds'] for r in runs}
    if len(durations)!=1 or next(iter(durations))<1: raise ValueError('Equal replay durations >= 1 second required')
    duration=next(iter(durations))
    traces={r['fps']:trajectory(base/r['poses'],r['replay_start_ns'],duration) for r in runs}
    metrics={str(rate):compare(traces[30],traces[rate],duration) for rate in (60,144)}
    # Pose telemetry alone cannot observe collisions, cloth vertices or weapon
    # durability. A numerical pass here must never be promoted to full acceptance.
    return dict(trajectory_checks=metrics,
                trajectory_accepted=all(m['horizontal_within_0_1_percent'] for m in metrics.values()),
                physics_accepted=False,missing_observations=['collision contacts','slope fixture',
                    'cloth vertices','particle lifetime','durability','damage windows'])


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifest',type=Path);parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args()
    try: result=evaluate(json.loads(args.manifest.read_text()),args.manifest.parent)
    except (ValueError,KeyError,OSError) as error: result=dict(physics_accepted=False,error=str(error))
    args.out.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))
    return 0 if result.get('trajectory_accepted') else 1

if __name__=='__main__': raise SystemExit(main())
