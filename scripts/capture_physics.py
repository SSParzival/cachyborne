#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Capture the same offline copied-save pad replay at 30, 60 and 144 FPS.

This records positions only. Use physics_acceptance.py for trajectory checks;
cloth, collision contacts and durability require additional instrumentation.
"""
import argparse
import copy
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import time
from stress_warp_soak import ROOT, Tail, core, hashes


def capture(profile, rate, replay, duration, out):
    out.mkdir(mode=0o700)
    profile=copy.deepcopy(profile)
    shutil.copytree(profile['user'],out/'user')
    profile['user']=str(out/'user')
    settings=profile['settings']
    settings.update(fps=str(rate),fps_limit=rate,ramdisk=False,gamemode=False,
                    gamescope=False,fullscreen=False,hdr=False)
    settings.setdefault('runtime_env',{}).update(BB_ONLINE='0',BB_BBSC='0',BB_FPS=str(rate),BB_FPS_LIMIT=str(rate))
    command,env=core.launch_command(profile)
    pad=out/'pad';pad.write_text('')
    env.update(BB_PAD_FILE=str(pad),BB_PAD_REPLAY=str(replay),BB_ACCEPTANCE_POSES=str(out/'poses.csv'),
               BB_FRAME_STATS='1',BB_SKIP_NETWORK_CHOICE='1',SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS='1')
    reader=Tail(out/'game.log');recent='';start=time.monotonic()
    pressed=False;world=None;replay_sent=None;anchor=None
    with (out/'game.log').open('w') as log:
        process=subprocess.Popen(['stdbuf','-oL','-eL',*command],cwd=ROOT,env=env,
                                 stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
        try:
            while time.monotonic()-start < 180+duration:
                time.sleep(.1)
                if process.poll() is not None: raise RuntimeError(f'{rate} FPS game exited {process.returncode}')
                recent=(recent+'\n'.join(reader.lines())+'\n')[-65536:]
                if re.search(r'STOP:|ErrorDeviceLost|Assertion Failed|Host fault|Guest fault',recent):
                    raise RuntimeError(f'{rate} FPS runtime fault, inspect game.log')
                elapsed=time.monotonic()-start
                if not pressed and elapsed>12 and 'pad opened' in recent:
                    pad.write_text('cross');time.sleep(.3);pad.write_text('');pressed=True
                stats=re.findall(r'Frame stats: ([0-9.]+) FPS.*? ([0-9]+) draws/frame',recent)
                if stats and int(stats[-1][1])>600 and world is None: world=time.monotonic()
                if world and replay_sent is None and time.monotonic()-world>5:
                    pad.write_text('replay');replay_sent=time.monotonic()
                match=re.search(r'Runtime: pad replay start ns (\d+)',recent)
                if match: anchor=int(match[1])
                if replay_sent and time.monotonic()-replay_sent > duration+3:
                    if not anchor: raise RuntimeError('Runtime did not acknowledge replay')
                    return dict(fps=rate,poses=f'{rate}/poses.csv',replay_start_ns=anchor,
                                duration_seconds=duration,observed_fps=[float(f) for f,_ in stats])
            raise RuntimeError(f'{rate} FPS capture timeout')
        finally:
            if process.poll() is None: os.killpg(process.pid,signal.SIGTERM)
            try: process.wait(timeout=10)
            except subprocess.TimeoutExpired: os.killpg(process.pid,signal.SIGKILL);process.wait()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile',type=Path);parser.add_argument('--replay',type=Path,required=True)
    parser.add_argument('--duration',type=float,required=True);parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args()
    if not 1<=args.duration<=120: parser.error('duration must be 1..120 seconds')
    profile=core.load_profile(args.profile);replay=args.replay.resolve()
    if not replay.is_file(): parser.error('replay file does not exist')
    out=args.out.resolve();out.mkdir(mode=0o700,parents=True,exist_ok=False)
    original=hashes(Path(profile['user'])/'savedata')
    manifest=dict(runs=[],original_saves_unchanged=False,
                  replay_sha256=__import__('hashlib').sha256(replay.read_bytes()).hexdigest())
    try:
        for rate in (30,60,144):
            result=capture(profile,rate,replay,args.duration,out/str(rate))
            manifest['runs'].append(result);print(json.dumps(result),flush=True)
    finally:
        manifest['original_saves_unchanged']=original==hashes(Path(profile['user'])/'savedata')
        (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    if not manifest['original_saves_unchanged']: raise RuntimeError('Original save changed')

if __name__=='__main__': main()
