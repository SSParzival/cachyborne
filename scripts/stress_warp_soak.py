#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Offline copied-save lamp travel acceptance, with continuous process/GPU/staging samples.

A pass requires actual map transitions and return-to-floor checks. Merely staying
alive, sending a warp command, or running a fixed delay cannot satisfy the test.
"""
import argparse
import copy
import csv
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'launcher'))
import cachyborne as core
from isolated_game import MemoryWatch, headless_command, isolate_profile, stop_group, fault_checks


class Tail:
    def __init__(self, path):
        self.path, self.offset, self.partial = Path(path), 0, ''

    def lines(self):
        try:
            with self.path.open(errors='replace') as file:
                file.seek(self.offset)
                text = self.partial + file.read()
                self.offset = file.tell()
        except FileNotFoundError:
            return []
        parts = text.split('\n')
        self.partial = parts.pop()
        return parts


def gpu_sample(devices, sysfs=Path('/sys/bus/pci/devices')):
    """Whole-board telemetry for the game's sole DRM device, including other apps."""
    result = dict(gpu_power_uw=None, gpu_busy_percent=None)
    if len(devices) != 1:
        return result
    address = next(iter(devices))
    if not re.fullmatch(r'[0-9a-fA-F]{4}:[0-9a-fA-F]{2}:[0-9a-fA-F]{2}\.[0-7]', address):
        return result
    root = sysfs / address
    try:
        busy = int((root / 'gpu_busy_percent').read_text())
        if 0 <= busy <= 100: result['gpu_busy_percent'] = busy
    except (OSError, ValueError):
        pass
    for path in sorted((root / 'hwmon').glob('hwmon*/power1_average')):
        try:
            power = int(path.read_text())
            if power >= 0:
                result['gpu_power_uw'] = power
                break
        except (OSError, ValueError):
            pass
    return result


def memory_sample(pid):
    """Kernel counters, deduplicating DRM clients and memfd inodes."""
    root = Path('/proc') / str(pid)
    status = (root / 'status').read_text()
    rss = int(re.search(r'^VmRSS:\s+(\d+)', status, re.M)[1]) * 1024
    rollup = (root / 'smaps_rollup').read_text()
    def kib(name):
        match = re.search(r'^' + name + r':\s+(\d+)', rollup, re.M)
        return int(match[1]) * 1024 if match else 0
    # Keep cumulative faults, CPU and actual disk I/O beside the memory samples
    # so a frame pause can be correlated without attributing it to the GPU.
    fields = (root / 'stat').read_text().rsplit(')', 1)[1].split()
    io = {name: int(value) for name, value in
          (line.split(':', 1) for line in (root / 'io').read_text().splitlines())}
    swap = re.search(r'^VmSwap:\s+(\d+)', status, re.M)
    clients, memfds, devices = set(), set(), set()
    vram = gtt = memfd_bytes = 0
    for fd in (root / 'fdinfo').iterdir():
        try:
            text = fd.read_text()
            client = re.search(r'^drm-client-id:\s+(\d+)', text, re.M)
            if client and client[1] not in clients:
                clients.add(client[1])
                device = re.search(r'^drm-pdev:\s*(\S+)', text, re.M)
                if device: devices.add(device[1])
                for name in ('vram', 'gtt'):
                    match = re.search(r'^drm-memory-' + name + r':\s+(\d+)\s+KiB', text, re.M)
                    if match:
                        if name == 'vram': vram += int(match[1]) * 1024
                        else: gtt += int(match[1]) * 1024
            link = root / 'fd' / fd.name
            if 'memfd:bb-guest-memory' in os.readlink(link):
                info = link.stat()
                key = (info.st_dev, info.st_ino)
                if key not in memfds:
                    memfds.add(key); memfd_bytes += info.st_blocks * 512
        except (FileNotFoundError, ProcessLookupError):
            continue
    return dict(rss=rss, anonymous=kib('Anonymous'), pss_shmem=kib('Pss_Shmem'),
                vram=vram, gtt=gtt, memfd_allocated=memfd_bytes,
                swap=int(swap[1])*1024 if swap else 0,
                minor_faults=int(fields[7]), major_faults=int(fields[9]),
                cpu_user_ticks=int(fields[11]), cpu_system_ticks=int(fields[12]),
                ticks_per_second=os.sysconf('SC_CLK_TCK'),
                disk_read_bytes=io.get('read_bytes', 0), disk_write_bytes=io.get('write_bytes', 0),
                **gpu_sample(devices))


def floor_checks(floors, rss_mib=256, vram_mib=512, staging_mib=32):
    if len(floors) < 2:
        return {'enough_cycles': False}
    baseline, final = floors[0], floors[-1]
    return {name: final[name] - baseline[name] <= limit * 1024**2
            for name, limit in [('rss', rss_mib), ('vram', vram_mib), ('staging', staging_mib)]}


def hashes(directory):
    return {str(p.relative_to(directory)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in directory.rglob('*') if p.is_file()}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile', type=Path)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--iterations', type=int, default=15)
    parser.add_argument('--settle', type=float, default=35)
    parser.add_argument('--transition-timeout', type=float, default=120)
    parser.add_argument('--lamps', nargs=2, type=int, help='Dream and Central Yharnam lamp IDs; otherwise discover')
    parser.add_argument('--max-rss-growth-mib', type=int, default=256)
    parser.add_argument('--max-vram-growth-mib', type=int, default=512)
    parser.add_argument('--max-staging-growth-mib', type=int, default=32)
    args = parser.parse_args(argv)
    if args.iterations < 1 or args.settle < 31 or args.transition_timeout < 10:
        parser.error('iterations >= 1, settle >= 31 seconds and transition-timeout >= 10 are required')
    profile = copy.deepcopy(core.load_profile(args.profile))
    memory_watch = MemoryWatch(core.available_memory)
    out = args.out.resolve(); out.mkdir(parents=True, mode=0o700, exist_ok=False)
    source_user = Path(profile['user'])
    original = hashes(source_user / 'savedata')
    isolated = out / 'user'; shutil.copytree(source_user, isolated)
    profile['user'] = str(isolated)
    isolate_profile(profile)
    s = profile['settings']
    s.update(fps='60', fps_limit=60, ramdisk=False, gamemode=False, gamescope=False, fullscreen=False, hdr=False)
    s.setdefault('runtime_env', {}).update(BB_ONLINE='0', BB_BBSC='0', BB_FPS='60', BB_FPS_LIMIT='60')
    command, env = core.launch_command(profile)
    direct_pool = core.direct_memory_mb(s)
    if int(env['BB_DMEM_MB']) != direct_pool:
        raise RuntimeError('La memoria directa no coincide con los heaps del perfil')
    pad = out / 'pad'; pad.write_text('')
    commands = out / 'command'; commands.write_text('')
    poses = out / 'poses.csv'
    env.update(BB_ACCEPTANCE_COMMAND=str(commands), BB_ACCEPTANCE_POSES=str(poses),
               BB_STAGING_STATS='1', BB_FRAME_STATS='1', BB_AUDIO_STATS='1',
               BB_PAD_FILE=str(pad), SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS='1', BB_SKIP_NETWORK_CHOICE='1')
    # Gamescope is the parent process, not the game whose DRM/RSS counters we
    # measure. The shell writes its own PID, then exec preserves it for bb-probe.
    pidfile = out / 'game.pid'
    command = ['sh', '-c', 'echo "$$" > "$1"; shift; exec "$@"',
               'isolated-game', str(pidfile), *command]
    command, env = headless_command(command, env, s['output_res'], 60)
    logpath = out / 'game.log'
    pose_reader, log_reader = Tail(poses), Tail(logpath)
    last_pose, lamps, pools, samples, transitions, floors = {}, {}, {}, [], [], []
    recent, columns, sequence = '', [], 0
    started = time.monotonic()
    result = dict(accepted=False, requested_cycles=args.iterations, direct_pool_mib=direct_pool,
                  render_res=s['render_res'], output_res=s['output_res'],
                  preset=str(s['runtime_ini']['preset']), isolated_display=True, audio=False,
                  settle_seconds=args.settle, transitions=transitions, floors=floors)
    with logpath.open('w') as log, (out / 'memory.csv').open('w') as sample_file:
        sample_csv = None
        process = subprocess.Popen(['stdbuf', '-oL', '-eL', *command], cwd=ROOT, env=env,
                                   stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
        def poll():
            nonlocal last_pose, columns, recent, sample_csv
            memory_watch.check()
            if process.poll() is not None:
                raise RuntimeError(f'Game exited early: {process.returncode}')
            lines = log_reader.lines()
            recent = (recent + '\n'.join(lines) + '\n')[-65536:]
            if not all(fault_checks(recent).values()):
                raise RuntimeError('Game fault; inspect game.log')
            for line in lines:
                if match := re.match(r'Acceptance lamp: (\d+) map (\d+)', line):
                    lamps[int(match[1])] = int(match[2])
                if match := re.match(r'Staging stats: pool=(\S+) cached=(\d+) uncached=(\d+) device=(\d+) vram=(\d+) budget=(\d+)', line):
                    pools[match[1]] = dict(zip(('cached', 'uncached', 'device', 'vulkan_usage', 'vulkan_budget'), map(int, match.groups()[1:])))
            for line in pose_reader.lines():
                row = next(csv.reader([line]))
                if row and row[0] == 'ns': columns = row
                elif columns and len(row) == len(columns):
                    last_pose = dict(zip(columns, row))
                    if int(last_pose['dropped']): raise RuntimeError('Pose telemetry dropped samples')
            if not pidfile.exists():
                return None # the compositor has not yet spawned its child
            sample = memory_sample(int(pidfile.read_text().strip()))
            sample.update(s=round(time.monotonic() - started, 3), cycle=len(floors),
                          map=int(last_pose.get('map', 0)), staging=sum(p['cached'] + p['uncached'] + p['device'] for p in pools.values()),
                          vulkan_usage=max((p['vulkan_usage'] for p in pools.values()), default=0),
                          vulkan_budget=max((p['vulkan_budget'] for p in pools.values()), default=0))
            if sample_csv is None:
                sample_csv = csv.DictWriter(sample_file, fieldnames=list(sample)); sample_csv.writeheader()
            sample_csv.writerow(sample); sample_file.flush(); samples.append(sample)
            return sample
        def wait_for(predicate, timeout):
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                sample = poll()
                if predicate(): return sample
                time.sleep(.5)
            raise RuntimeError('Acceptance transition/telemetry timed out')
        def send(action):
            nonlocal sequence
            sequence += 1
            temporary = commands.with_suffix('.tmp'); temporary.write_text(f'{sequence} {action}\n')
            temporary.replace(commands)
        def ready():
            return last_pose.get('valid') == '1' and last_pose.get('loading') == '0'
        try:
            wait_for(lambda: 'pad opened' in recent, 45)
            time.sleep(10); pad.write_text('cross'); time.sleep(.3); pad.write_text('')
            wait_for(ready, 90)
            if args.lamps:
                selected = args.lamps
            else:
                send('list'); wait_for(lambda: f'Acceptance command: {sequence} listed' in recent, 60)
                if 2102950 not in lamps or 2412951 not in lamps:
                    raise RuntimeError('Default lamps absent; choose verified IDs using --lamps (see game.log)')
                selected = [2102950, 2412951]
            result['lamps'] = selected
            def warp(lamp):
                before = int(last_pose['map']); begin = time.monotonic(); send(f'warp {lamp}')
                wait_for(lambda: f'Acceptance command: {sequence} ' in recent, 10)
                if f'Acceptance command: {sequence} rejected' in recent: raise RuntimeError('Guest rejected lamp')
                # A map change, not just the request acknowledgement, certifies travel.
                wait_for(lambda: ready() and int(last_pose['map']) != before, args.transition_timeout)
                reached = int(last_pose['map'])
                settle_until = time.monotonic() + args.settle
                while time.monotonic() < settle_until:
                    poll(); time.sleep(.5)
                if not ready() or int(last_pose['map']) != reached:
                    raise RuntimeError('World did not remain settled after travel')
                transitions.append(dict(sequence=sequence, lamp=lamp, from_map=before, to_map=reached,
                                        elapsed_s=round(time.monotonic() - begin, 3)))
                return poll()
            # Normalize the initial location without counting a same-map request as travel.
            if int(last_pose['map']) >> 24 != 21:
                warp(selected[0])
            for cycle in range(args.iterations):
                warp(selected[1]); floor = warp(selected[0]); floors.append(floor)
                print(f'Cycle {cycle+1}/{args.iterations}: RSS {floor["rss"]/2**20:.1f} MiB, VRAM {floor["vram"]/2**20:.1f} MiB, staging {floor["staging"]/2**20:.1f} MiB', flush=True)
            checks = floor_checks(floors, args.max_rss_growth_mib, args.max_vram_growth_mib, args.max_staging_growth_mib)
            checks['staging_observed'] = bool(pools)
            checks['vram_observed'] = any(s['vram'] > 0 for s in samples)
            checks['fifteen_cycles'] = len(floors) >= 15
            result.update(checks=checks, accepted=all(checks.values()))
        except (OSError, RuntimeError, ValueError) as error:
            result['error'] = str(error)
        finally:
            print('Acceptance: stopping isolated processes', file=log, flush=True)
            stop_group(process)
            result.setdefault('checks',{}).update(fault_checks(logpath.read_text(errors='replace')))
            result['accepted'] &= all(result['checks'].values())
            result.update(exit_code=process.returncode, elapsed_s=round(time.monotonic()-started, 3),
                          original_saves_unchanged=hashes(source_user / 'savedata') == original,
                          samples=len(samples), peak_rss=max((s['rss'] for s in samples), default=0),
                          peak_vram=max((s['vram'] for s in samples), default=0))
            result['accepted'] &= result['original_saves_unchanged']
            (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({k:v for k,v in result.items() if k not in ('transitions', 'floors')}, indent=2))
    return 0 if result['accepted'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
