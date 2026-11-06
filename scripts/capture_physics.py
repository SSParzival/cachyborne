#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Capture the same offline copied-save pad replay at 30, 60 and 144 FPS.

This records positions only. Use physics_acceptance.py for trajectory checks;
cloth, collision contacts and durability require additional instrumentation.
"""
import argparse
import copy
import csv
from contextlib import nullcontext
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time
from stress_warp_soak import ROOT, Tail, core, hashes, memory_sample
from isolated_game import desktop_command, headless_command, isolate_profile, isolate_configuration, stop_group, MemoryWatch, fault_checks, SnapshotGate


def capture(profile, rate, replay, duration, out, warmup=5, require_backend=None, *, audio=False, host_trace=False, audio_dump=True, warp_lamp=None, expected_map=None, presentation='headless', monitor=None):
    if (warp_lamp is None) != (expected_map is None):
        raise ValueError('Area capture requires both a lamp and an expected map')
    if presentation not in ('headless','desktop') or (monitor is not None and presentation!='desktop'):
        raise ValueError('Monitor selection requires explicit desktop presentation')
    memory_watch=MemoryWatch(core.available_memory)
    out.mkdir(mode=0o700)
    profile=copy.deepcopy(profile)
    isolate_profile(profile, audio=audio, desktop=presentation=='desktop')
    shutil.copytree(profile['user'],out/'user')
    profile['user']=str(out/'user')
    settings=profile['settings']
    settings.update(fps=str(rate),fps_limit=rate,ramdisk=False,gamemode=False,gamescope=False)
    settings.setdefault('runtime_env',{}).update(BB_ONLINE='0',BB_BBSC='0',BB_FPS=str(rate),BB_FPS_LIMIT=str(rate))
    command,env=core.launch_command(profile)
    env=isolate_configuration(env,out)
    if monitor is not None: env['BB_DISPLAY']=str(monitor)
    pad=out/'pad';pad.write_text('')
    frames=out/'frames';frames.mkdir()
    trigger=out/'capture'
    snapshot=SnapshotGate(frames,settings['output_res'])
    env.update(BB_PAD_FILE=str(pad),BB_PAD_REPLAY=str(replay),BB_ACCEPTANCE_POSES=str(out/'poses.csv'),
               BB_FINAL_DUMP_TRIGGER=str(trigger),BB_DUMP_DIR=str(frames),BB_FRAME_LOG=str(out/'frames.csv'),
               BB_FRAME_STATS='1',BB_SKIP_NETWORK_CHOICE='1',SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS='1')
    commands=out/'command'
    if warp_lamp is not None:
        commands.write_text('')
        env['BB_ACCEPTANCE_COMMAND']=str(commands)
    if audio:
        env['BB_AUDIO_STATS']='1'
        if audio_dump: env['BB_AUDIO_DUMP']=str(out/'audio')
        else: env.pop('BB_AUDIO_DUMP',None)
    pidfile=out/'game.pid'
    # Desktop device observers need the owned game's PID even without resource
    # sampling. exec keeps this PID when the shell becomes the actual game.
    if host_trace or presentation=='desktop':
        command=['sh','-c','echo "$$" > "$1"; shift; exec "$@"',
                 'isolated-game',str(pidfile),*command]
    if presentation=='desktop': command,env=desktop_command(command,env,audio=audio)
    else: command,env=headless_command(command,env,settings['output_res'],rate,audio=audio)
    reader=Tail(out/'game.log');recent='';start=time.monotonic()
    last_press=0;world=None;replay_sent=None;anchor=None
    backend_ready=require_backend is None;replay_finished=False;audio_ready=not audio
    audio_backend='none' if not audio else None
    trace=(out/'host-resources.jsonl').open('x') if host_trace else nullcontext()
    last_resource=0
    pose_reader=Tail(out/'poses.csv');columns=[];last_pose={};warp_sent=False
    with (out/'game.log').open('w') as log, trace as resources:
        process=subprocess.Popen(['stdbuf','-oL','-eL',*command],cwd=ROOT,env=env,
                                 stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
        try:
            while time.monotonic()-start < 180+duration:
                time.sleep(.1)
                memory_watch.check()
                if process.poll() is not None: raise RuntimeError(f'{rate} FPS game exited {process.returncode}')
                now=time.monotonic()
                if resources and pidfile.exists() and now-last_resource>=1:
                    try:
                        sample=memory_sample(int(pidfile.read_text().strip()))
                    except (FileNotFoundError,ProcessLookupError):
                        sample=None
                    if sample is not None:
                        sample.update(monotonic_ns=time.monotonic_ns(),available=core.available_memory(),
                                      host_full_stalled_us=memory_watch.stalled_us())
                        resources.write(json.dumps(sample)+'\n');resources.flush()
                    last_resource=now
                recent=(recent+'\n'.join(reader.lines())+'\n')[-65536:]
                if warp_lamp is not None:
                    for line in pose_reader.lines():
                        row=next(csv.reader([line]))
                        if row and row[0]=='ns': columns=row
                        elif columns and len(row)==len(columns): last_pose=dict(zip(columns,row))
                    ready=last_pose.get('valid')=='1' and last_pose.get('loading')=='0'
                    if ready and not warp_sent:
                        if int(last_pose['map'])==expected_map:
                            raise RuntimeError('Already in target map; this capture requires an actual area transition')
                        temporary=commands.with_suffix('.tmp')
                        temporary.write_text(f'1 warp {warp_lamp}\n');temporary.replace(commands)
                        warp_sent=True
                    if 'Acceptance command: 1 rejected' in recent:
                        raise RuntimeError('Game rejected area capture lamp')
                    arrived=warp_sent and ready and int(last_pose.get('sequence','0'))>=1 and int(last_pose['map'])==expected_map
                    if not arrived: world=None
                if not all(fault_checks(recent).values()):
                    raise RuntimeError(f'{rate} FPS runtime fault, inspect game.log')
                actual_audio=re.search(r'Runtime: audio backend (.+)',recent)
                if actual_audio: audio_backend=actual_audio[1].strip()
                if audio and actual_audio:
                    if presentation=='headless' and audio_backend!='dummy':
                        raise RuntimeError('Isolated audio backend is not dummy; inspect game.log')
                    if presentation=='desktop' and audio_backend in ('dummy','disk','none'):
                        raise RuntimeError('Desktop capture has no real audio backend; inspect game.log')
                if presentation=='desktop' and monitor is not None and re.search(r'Display: BB_DISPLAY=.* matches none',recent):
                    raise RuntimeError('Requested desktop monitor was not selected; inspect game.log')
                audio_ready = audio_ready or bool(actual_audio)
                elapsed=time.monotonic()-start
                if world is None and not warp_sent and elapsed>12 and elapsed-last_press>6 and 'pad opened' in recent:
                    pad.write_text('cross');time.sleep(.3);pad.write_text('');last_press=elapsed
                stats=re.findall(r'Frame stats: ([0-9.]+) FPS.*? ([0-9]+) draws/frame',recent)
                world_ready = arrived if warp_lamp is not None else bool(stats and int(stats[-1][1])>600)
                if world is None and world_ready: world=time.monotonic()
                backend_ready = backend_ready or require_backend in recent
                replay_finished = replay_finished or 'Runtime: pad replay finished' in recent
                if world and replay_sent is None and time.monotonic()-world>warmup and backend_ready and audio_ready:
                    now=time.monotonic()
                    if snapshot.requested is None:
                        snapshot.request(trigger,now)
                    elif snapshot.ready(now):
                        pad.write_text('replay');replay_sent=now
                match=re.search(r'Runtime: pad replay start ns (\d+)',recent)
                if match: anchor=int(match[1])
                if replay_sent and time.monotonic()-replay_sent > duration+3:
                    if not anchor: raise RuntimeError('Runtime did not acknowledge replay')
                    if not replay_finished:
                        continue
                    return dict(fps=rate,poses=f'{rate}/poses.csv',replay_start_ns=anchor,
                                duration_seconds=duration,observed_fps=[float(f) for f,_ in stats],
                                render_res=settings['render_res'],output_res=settings['output_res'],
                                upscaler=settings['upscaler'],preset=str(settings['runtime_ini']['preset']),
                                frame_generation=settings['runtime_ini'].get('frame_generation','off'),
                                direct_memory_mb=core.direct_memory_mb(settings),
                                guest_vram=settings['runtime_env'].get('BB_GUEST_VRAM','0'),
                                audio_backend=audio_backend,host_trace=host_trace,
                                configuration=env['BB_CONFIG'],
                                snapshot_before_replay=True,snapshot_quiet_seconds=snapshot.quiet,
                                presentation=presentation,monitor=env.get('BB_DISPLAY'),
                                hdr_requested=env.get('BB_HDR')=='1',fullscreen_requested=env.get('BB_FULLSCREEN')=='1',
                                audio_dump=bool(audio and audio_dump),warp_lamp=warp_lamp,expected_map=expected_map)
            raise RuntimeError(f'{rate} FPS capture timeout')
        finally:
            print('Acceptance: stopping isolated processes',file=log,flush=True)
            stop_group(process)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile',type=Path);parser.add_argument('--replay',type=Path,required=True)
    parser.add_argument('--duration',type=float,required=True);parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--warmup',type=float,default=5)
    parser.add_argument('--require-backend',help='Renderer log marker required before measuring the route')
    parser.add_argument('--audio',action='store_true',help='Exercise SDL dummy audio and dump PCM without desktop playback')
    parser.add_argument('--no-audio-dump',dest='audio_dump',action='store_false',help='Exercise --audio without writing PCM (reduce diagnostic disk I/O)')
    parser.add_argument('--host-trace',action='store_true',help='Sample game memory, faults, disk I/O and host pressure once per second (diagnostic overhead)')
    parser.add_argument('--desktop',action='store_true',help='Explicitly show the copied-save capture on the desktop, using real audio with --audio')
    parser.add_argument('--monitor',help='SDL display number/name; requires --desktop')
    parser.add_argument('--warp-lamp',type=int,help='Travel to this lamp before warmup, using copied saves')
    parser.add_argument('--expected-map',type=lambda value:int(value,0),help='Required destination map from pose telemetry (decimal or 0x hex)')
    args=parser.parse_args()
    if not 1<=args.duration<=120: parser.error('duration must be 1..120 seconds')
    if not 0<=args.warmup<=120: parser.error('warmup must be 0..120 seconds')
    if (args.warp_lamp is None)!=(args.expected_map is None): parser.error('--warp-lamp and --expected-map must be supplied together')
    if args.monitor is not None and not args.desktop: parser.error('--monitor requires --desktop')
    profile=core.load_profile(args.profile);replay=args.replay.resolve()
    if not replay.is_file(): parser.error('replay file does not exist')
    out=args.out.resolve();out.mkdir(mode=0o700,parents=True,exist_ok=False)
    original=hashes(Path(profile['user'])/'savedata')
    manifest=dict(runs=[],original_saves_unchanged=False,
                  replay_sha256=__import__('hashlib').sha256(replay.read_bytes()).hexdigest())
    try:
        for rate in (30,60,144):
            result=capture(profile,rate,replay,args.duration,out/str(rate),args.warmup,args.require_backend,audio=args.audio,host_trace=args.host_trace,audio_dump=args.audio_dump,warp_lamp=args.warp_lamp,expected_map=args.expected_map,presentation='desktop' if args.desktop else 'headless',monitor=args.monitor)
            manifest['runs'].append(result);print(json.dumps(result),flush=True)
    finally:
        manifest['original_saves_unchanged']=original==hashes(Path(profile['user'])/'savedata')
        (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    if not manifest['original_saves_unchanged']: raise RuntimeError('Original save changed')

if __name__=='__main__': main()
