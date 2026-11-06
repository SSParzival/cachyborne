# SPDX-License-Identifier: GPL-2.0-or-later
"""Acceptance presentation, host pressure limits and owned-process cleanup."""
import os
import re
import shutil
import signal
import subprocess
import time
from pathlib import Path


def isolate_configuration(environment, directory):
    """Give an owned capture a writable INI without altering the prepared one."""
    result = dict(environment)
    source = Path(result['BB_CONFIG'])
    destination = Path(directory) / 'bbport.ini'
    if source.resolve() == destination.resolve():
        raise ValueError('Capture configuration must be separate from its source')
    with source.open('rb') as reader:
        fd = os.open(destination, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(fd, 'wb') as writer:
            shutil.copyfileobj(reader, writer)
    result['BB_CONFIG'] = str(destination.resolve())
    return result


class SnapshotGate:
    """Complete the diagnostic GPU readback before opening a timed replay.

    The first readback can allocate a mapped VMA block and write a large file.
    It is evidence about the image, not normal game presentation work.
    """
    def __init__(self, frames, resolution, *, quiet_seconds=2, timeout_seconds=30):
        width, height = map(int, resolution.split('x'))
        if width <= 0 or height <= 0:
            raise ValueError('Snapshot resolution must be positive')
        self.paths = [Path(frames)/f'f000_final_{width}x{height}_{fmt}.raw' for fmt in ('bgra', 'rgba')]
        self.size = width * height * 4
        self.quiet = quiet_seconds
        self.timeout = timeout_seconds
        self.requested = self.complete = None
        self.signature = None

    def request(self, trigger, now):
        if self.requested is not None:
            raise RuntimeError('Snapshot already requested')
        if any(path.exists() for path in self.paths):
            raise RuntimeError('Pre-replay snapshot output already exists')
        Path(trigger).touch()
        self.requested = now

    def ready(self, now):
        if self.requested is None:
            return False
        if now - self.requested > self.timeout:
            raise RuntimeError('Pre-replay snapshot did not complete before timeout')
        signatures = []
        for path in self.paths:
            try:
                stat = path.stat()
            except FileNotFoundError:
                continue
            if stat.st_size > self.size:
                raise RuntimeError('Pre-replay snapshot exceeds the expected image size')
            if stat.st_size == self.size:
                signatures.append((str(path), stat.st_mtime_ns))
        signature = tuple(signatures)
        if len(signature) != 1:
            self.complete = self.signature = None
            return False
        if signature != self.signature:
            self.signature, self.complete = signature, now
        return now - self.complete >= self.quiet


def fault_checks(log):
    """Compositor shutdown may lose a surface, but must not hide worker faults."""
    before_stop = log.split('Acceptance: stopping isolated processes', 1)[0]
    faults = r'ErrorDeviceLost|Device lost while|Assertion Failed|Host fault|Guest fault|Fault \(signal \d+\)'
    return dict(runtime_faults_absent=not re.search('STOP:|'+faults,before_stop),
                shutdown_faults_absent=not re.search(faults,log))


class MemoryWatch:
    """Abort our game when host memory pressure would compete with the active desktop."""
    def __init__(self, available):
        self.available = available
        if available() < 4 << 30:
            raise RuntimeError('Prueba pospuesta: se requieren 4 GiB disponibles en host.')
        self.at = time.monotonic()
        self.pressure = self.stalled_us()

    @staticmethod
    def stalled_us():
        try:
            line = next(line for line in Path('/proc/pressure/memory').read_text().splitlines()
                        if line.startswith('full '))
            return int(line.rsplit('total=', 1)[1])
        except (OSError, ValueError, StopIteration):
            return 0

    def check(self):
        if self.available() < 2 << 30:
            raise RuntimeError('Prueba detenida: menos de 2 GiB disponibles en host.')
        now = time.monotonic()
        if now-self.at >= 5:
            pressure = self.stalled_us()
            if pressure-self.pressure > (now-self.at)*250000:
                raise RuntimeError('Prueba detenida: presión de memoria del host superior al 25%.')
            self.at, self.pressure = now, pressure


def isolate_profile(profile, *, audio=False, desktop=False):
    settings = profile['settings']
    settings.update(gamescope=False, gamemode=False, audio=audio, ramdisk=False)
    if not desktop:
        settings.update(hdr=False, vrr=False, fullscreen=False)


def desktop_command(command, env, *, audio=False):
    """Explicit desktop verification; never count a dummy device as real output."""
    env = env.copy()
    if not (env.get('WAYLAND_DISPLAY') or env.get('DISPLAY')):
        raise RuntimeError('Desktop verification requires an active compositor connection')
    if any(env.get(key) in ('dummy', 'offscreen') for key in ('SDL_VIDEO_DRIVER','SDL_VIDEODRIVER')):
        raise RuntimeError('Desktop verification cannot use a dummy/offscreen video driver')
    if audio and any(env.get(key) in ('dummy', 'disk', 'none')
                     for key in ('SDL_AUDIO_DRIVER', 'SDL_AUDIODRIVER')):
        raise RuntimeError('Desktop audio verification cannot use a dummy/disk audio driver')
    env.update(BB_AUDIO='auto' if audio else 'none', BB_ONLINE='0', BB_BBSC='0')
    return list(command), env


def headless_command(command, env, size='2560x1440', fps=60, *, audio=False):
    gamescope = shutil.which('gamescope')
    if not gamescope:
        raise RuntimeError('La prueba aislada requiere gamescope con backend headless.')
    env = env.copy()
    # No inherited route to the desktop, even if compositor startup fails.
    for key in ('DISPLAY', 'WAYLAND_DISPLAY', 'WAYLAND_SOCKET', 'GAMESCOPE_WAYLAND_DISPLAY'):
        env.pop(key, None)
    env.update(BB_AUDIO='auto' if audio else 'none', SDL_AUDIO_DRIVER='dummy',
               SDL_VIDEO_DRIVER='wayland', SDL_VIDEODRIVER='wayland',
               BB_HDR='0', BB_FULLSCREEN='0', BB_ONLINE='0', BB_BBSC='0')
    width, height = map(int, size.split('x'))
    if width <= 0 or height <= 0 or not 1 <= int(fps) <= 1000:
        raise ValueError('Invalid isolated display size/rate')
    if audio:
        # gamescope rewrites SDL's audio hints for its child (PipeWire on
        # this host). Set both SDL3 and legacy names *after* that rewrite.
        command = ['env', 'SDL_AUDIO_DRIVER=dummy', 'SDL_AUDIODRIVER=dummy', *command]
    return [gamescope, '--backend', 'headless', '--expose-wayland',
            '-W', str(width), '-H', str(height),
            '-w', str(width), '-h', str(height), '-r', str(fps), '--', *command], env


def stop_group(process):
    """The compositor and game share the session created by Popen."""
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        pass
    try:
        process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        pass
    # Waiting for the compositor only reaps the session leader. Its reaper or
    # game may still be alive after it exits, including after a memory abort.
    # Always finish the owned group, even when the leader has already exited.
    try:
        os.killpg(process.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    process.wait()
