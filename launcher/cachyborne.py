#!/usr/bin/env python3
"""Linux setup and direct execution. Launch never imports the ingestion tools."""
import argparse
import fcntl
import json
import shlex
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
CONFIG = Path(os.environ.get('XDG_CONFIG_HOME', Path.home() / '.config')) / 'cachyborne'
DATA = Path(os.environ.get('XDG_DATA_HOME', Path.home() / '.local/share')) / 'cachyborne'
RAM_ROOT = Path('/dev/shm') / f'cachyborne_ram_{os.getuid()}'
CACHE = Path(os.environ.get('XDG_CACHE_HOME', Path.home() / '.cache')) / 'cachyborne'
DEFAULTS = dict(memory_profile='16gb', pinning=True, p_cpus='0-11', e_cpus='12-19', ramdisk=False,
                gamemode=False, radv=True, gamescope=False, hdr=False, vrr=False,
                gamescope_fsr=False, width=2560, height=1440, fps='60',
                upscaler='off', output_res='2560x1440', render_res='1920x1080',
                fullscreen=True, present_mode='mailbox', audio=True, gamepad='',
                scheduler='current', fsr411_dir='', mods_dir='', mods_config='', patches_dir='', patches_config='')
ARTIFACTS = ('boot-linked.bin', 'patches.bin', 'content.bin', 'bbport.ini')


def read_json(path, fallback=None):
    try:
        return json.loads(path.read_text())
    except FileNotFoundError:
        if fallback is not None:
            return fallback
        raise RuntimeError('Run cachyborne-setup first: no prepared game profile.')


def atomic_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    fd, temporary = tempfile.mkstemp(dir=path.parent, prefix='.profile-')
    try:
        with os.fdopen(fd, 'w') as stream:
            json.dump(value, stream, indent=2)
            stream.write('\n')
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def preferences():
    prepared = read_json(CONFIG / 'game_profile.json', {}).get('settings', {})
    return DEFAULTS | prepared | read_json(CONFIG / 'settings.json', {})


def validate_settings(s):
    import re
    for key in ('p_cpus', 'e_cpus'):
        if not re.fullmatch(r'\d+(?:-\d+)?(?:,\d+(?:-\d+)?)*', s[key]):
            raise ValueError(f'Invalid CPU list: {key}')
    if s['memory_profile'] != '16gb':
        raise ValueError('Unsupported memory profile; expected 16gb')
    if s['fps'] not in ('30', '60', '90', '120', '144', '165', 'uncap'):
        raise ValueError('Unsupported FPS')
    if s['upscaler'] not in ('off', 'fsr3', 'fsr411'):
        raise ValueError('Unsupported upscaler')
    for key in ('output_res', 'render_res'):
        match = re.fullmatch(r'(\d+)x(\d+)', s[key])
        if not match or not (320 <= int(match[1]) <= 16384 and 200 <= int(match[2]) <= 16384):
            raise ValueError(f'Invalid resolution: {key}')
    if s['present_mode'] not in ('fifo', 'mailbox', 'immediate'):
        raise ValueError('Unsupported presentation mode')
    if s['scheduler'] not in ('current', 'scx_lavd', 'scx_bpfland'):
        raise ValueError('Unsupported scheduler')
    for key in ('width', 'height'):
        if not 200 <= int(s[key]) <= 16384:
            raise ValueError(f'Invalid Gamescope {key}')


def setup(game, settings=None):
    s = preferences() if settings is None else settings.copy()
    validate_settings(s)
    if os.environ.get('BB_SKIP_GAME_CHECK') == '1':
        raise RuntimeError('Setup requires validation; unset BB_SKIP_GAME_CHECK.')
    game = Path(game).expanduser().resolve(strict=True)
    if not (ROOT / 'out/bb-probe').is_file():
        raise RuntimeError('Build first: bash build.sh')
    sys.path.insert(0, str(ROOT / 'scripts'))
    import game_check
    image_hash = game_check.image_sha256(game)
    problem = game_check.problem(game, image_hash)
    if problem:
        raise RuntimeError(game_check.explain(*problem))
    subprocess.run([sys.executable, str(ROOT / 'scripts/game_check.py'), str(game)], check=True)
    if s['upscaler'] == 'fsr411':
        assets = Path(s['fsr411_dir'] or ROOT / 'fsr4_411').expanduser().resolve()
        # The full replay asset set is checked by the renderer; do not advertise absent assets.
        from bbport_assets import fsr411_problem
        issue = fsr411_problem(assets, s['output_res'], 1)
        if issue:
            raise RuntimeError(f'FSR 4.1.1: {issue}. Use tools/fsr4cap/build_assets.sh.')
        s['fsr411_dir'] = str(assets)
    DATA.mkdir(parents=True, exist_ok=True, mode=0o700)
    generations = DATA / 'prepared'
    generations.mkdir(exist_ok=True)
    generation = uuid.uuid4().hex
    out = generations / generation
    out.mkdir(mode=0o700)
    try:
        source_game = game
        if s['mods_dir']:
            import mods
            selection = mods.selected(Path(s['mods_dir']).expanduser(),
                                      Path(s['mods_config']).expanduser() if s['mods_config'] else CONFIG / 'mods.json')
            game = mods.build_overlay(game, out, selection)
            # Persist this generation's symlink view. Sources must remain in place until next setup.
            problem = game_check.problem(game)
            if problem:
                raise RuntimeError(game_check.explain(*problem))
        ini = out / 'bbport.ini'
        ini.write_text(f"upscaler={s['upscaler']}\noutput_res={s['output_res']}\nlive_resolution=0\n")
        for script in ('prepare.py', 'link_libc.py', 'link_modules.py', 'content_profile.py'):
            subprocess.run([sys.executable, str(ROOT / 'scripts' / script), str(game), '--out', str(out)],
                           cwd=ROOT, check=True)
        fps = s['fps'] if s['fps'] in ('30', '60', '90') else 'uncap'
        patch_args = []
        if s['patches_dir']:
            patch_args += ['--patches-dir', str(Path(s['patches_dir']).expanduser())]
        if s['patches_config']:
            patch_args += ['--patches-config', str(Path(s['patches_config']).expanduser())]
        subprocess.run([sys.executable, str(ROOT / 'scripts/patches.py'), '--out', str(out),
                        '--game-dir', str(game), '--fps', fps, '--settings', str(ini),
                        '--render-res', s['render_res'], '--output-res', s['output_res'], *patch_args], cwd=ROOT, check=True)
        for name in ARTIFACTS:
            if not (out / name).is_file() or not (out / name).stat().st_size:
                raise RuntimeError(f'Setup did not create {name}')
        user = DATA / 'user'
        user.mkdir(exist_ok=True)
        CACHE.mkdir(parents=True, exist_ok=True, mode=0o700)
        profile = dict(version=1, generation=generation, game=str(game), image_sha256=image_hash,
                       source_game=str(source_game), prepared=str(out), probe=str(ROOT / 'out/bb-probe'), user=str(user),
                       cache=str(CACHE), settings=s)
        if s['ramdisk']:
            stage(profile)
        publish_profile(profile)
    except BaseException:
        shutil.rmtree(out)
        raise
    print(f'Juego preparado: {game}', flush=True)
    return profile


def load_profile(path=None):
    profile = read_json(path or CONFIG / 'game_profile.json')
    if profile.get('version') != 1:
        raise RuntimeError('Unsupported profile version; run setup again.')
    if not (Path(profile['game']) / 'eboot.bin').is_file():
        raise RuntimeError('Game directory unavailable; run setup if it has moved.')
    if not os.access(profile['probe'], os.X_OK):
        raise RuntimeError('bb-probe unavailable; build and run setup again.')
    for name in ARTIFACTS:
        if not (Path(profile['prepared']) / name).is_file():
            raise RuntimeError(f'Missing {name}; run setup again.')
    return profile


def private_dir(path):
    """Do not follow shared /dev/shm symlinks or reuse someone else's tree."""
    try:
        path.mkdir(mode=0o700)
    except FileExistsError:
        pass
    info = path.lstat()
    if not stat.S_ISDIR(info.st_mode) or info.st_uid != os.getuid() or info.st_mode & 0o077:
        raise RuntimeError(f'Unsafe staging directory: {path}')


def available_memory():
    return next(int(line.split()[1]) * 1024 for line in Path('/proc/meminfo').read_text().splitlines()
                if line.startswith('MemAvailable:'))


def stage(profile):
    start = time.monotonic()
    # User-specific root avoids collisions between sessions owned by different users.
    base = RAM_ROOT
    private_dir(base)
    if not all(c in '0123456789abcdef' for c in profile['generation']) or len(profile['generation']) != 32:
        raise RuntimeError('Invalid staging generation')
    target = base / profile['generation']
    private_dir(target)
    if not any(line.split()[1:3] == ['/dev/shm', 'tmpfs'] for line in Path('/proc/mounts').read_text().splitlines()):
        raise RuntimeError('/dev/shm must be mounted as tmpfs.')
    lock_fd = os.open(base / 'stage.lock', os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW, 0o600)
    with os.fdopen(lock_fd, 'w') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        marker = target / '.cachyborne-complete'
        if marker.is_symlink():
            raise RuntimeError('Unsafe staging marker')
        if marker.is_file() and marker.read_text() == profile['game']:
            return target
        game = Path(profile['game'])
        # du -L follows the persistent setup mod view and includes linked subdirectories.
        size = int(subprocess.check_output(['du', '-sbL', '--', str(game)], text=True).split()[0])
        reserve = 8 * 1024**3
        available = available_memory()
        # Conservatively budget the entire copy even when a previous copy was interrupted.
        if shutil.disk_usage(base).free < size + 1024**3 or available < size + reserve:
            raise RuntimeError(f'Insufficient tmpfs/RAM space for {size / 1024**3:.1f} GiB; keep 8 GiB free.')
        print(f'Precargando {size / 1024**3:.1f} GiB en {target}…', flush=True)
        subprocess.run(['rsync', '-rltL', '--delete', '--info=progress2', '--', str(game) + '/', str(target) + '/'], check=True)
        marker.write_text(profile['game'])
        print(f'RAM lista: {time.monotonic() - start:.1f} s', flush=True)
    return target


def launch_command(profile, app0=None, extra=(), base_env=None):
    s = profile['settings']
    validate_settings(s)
    env = (os.environ if base_env is None else base_env).copy()
    out = Path(profile['prepared'])
    render_width, render_height = map(int, s['render_res'].split('x'))
    direct_mb = '5056' if render_width * render_height <= 1920 * 1080 else '9152'
    values = dict(BB_CONFIG=str(out / 'bbport.ini'), BB_GPU_USER_DIR=profile['user'],
                  MESA_SHADER_CACHE_DIR=profile['cache'], MESA_SHADER_CACHE_MAX_SIZE='4G',
                  BB_CPU_PINNING='1' if s['pinning'] else '0', BB_P_CPUS=s['p_cpus'], BB_E_CPUS=s['e_cpus'],
                  BB_RENDER_RES=s['render_res'], BB_OUTPUT_RES=s['output_res'],
                  BB_FULLSCREEN='1' if s['fullscreen'] else '0', BB_PRESENT_MODE=s['present_mode'].capitalize(),
                  BB_FPS=s['fps'], BB_FPS_LIMIT='0' if s['fps'] == 'uncap' else s['fps'],
                  BB_VBLANK_HZ='60' if s['fps'] == '30' else '1000', BB_DMEM_MB=direct_mb,
                  BB_DMEM_EXTRA_MB='512', BB_MEMORY_WILLNEED='0', BB_ASYNC_PIPELINE_THREADS='2',
                  BB_PC_MODEL='1', BB_GUEST_IN_PLACE='1', BB_PREUPLOAD='1',
                  BB_COPY_GPU_BUFFERS='1', BB_GPU_WRITE_TWINS='1', BB_GPU_WRITE_TWINS_MAX='65536',
                  BB_UFFD='0', BB_AUDIO='auto' if s['audio'] else 'none', BB_GAMEPAD=s['gamepad'])
    if s['fsr411_dir']:
        values['BB_FSR411_DIR'] = s['fsr411_dir']
    if s['radv']:
        values.update(AMD_VULKAN_ICD='RADV', RADV_PERFTEST='sam', RADV_TEX_ANISO='16',
                      VK_LOADER_LAYERS_DISABLE='~implicit~')
        for name in ('radeon_icd.x86_64.json', 'radeon_icd.json'):
            icd = Path('/usr/share/vulkan/icd.d') / name
            if icd.is_file():
                values['VK_DRIVER_FILES'] = str(icd)
                break
    for key, value in values.items():
        env.setdefault(key, value)
    # Pinned generation paths and CPU switch must follow the validated profile.
    for key in ('BB_CONFIG', 'BB_GPU_USER_DIR', 'BB_CPU_PINNING', 'BB_P_CPUS', 'BB_E_CPUS', 'BB_RENDER_RES', 'BB_OUTPUT_RES'):
        env[key] = values[key]
    command = [profile['probe'], str(out / 'boot-linked.bin'), '--content-profile', str(out / 'content.bin'),
               '--patches', str(out / 'patches.bin'), '--app0', str(app0 or profile['game']),
               '--user', profile['user'], '--timeout', '0', *extra]
    if s['gamemode']:
        command = ['gamemoderun', *command]
    if s['gamescope']:
        wrapper = ['gamescope', '-W', str(s['width']), '-H', str(s['height'])]
        if s['fullscreen']:
            wrapper += ['-f']
        if s['hdr']:
            wrapper += ['--hdr-enabled']
        if s['vrr']:
            wrapper += ['--adaptive-sync']
        if s['gamescope_fsr']:
            wrapper += ['-F', 'fsr']
        command = [*wrapper, '--', *command]
    return command, env


def publish_profile(profile):
    """Publish one symlink last: JSON and shell launch plan always name one generation."""
    out = Path(profile['prepared'])
    atomic_json(out / 'game_profile.json', profile)
    command, env = launch_command(profile, base_env={})
    quote = shlex.quote
    fixed = {'BB_CONFIG', 'BB_GPU_USER_DIR', 'BB_CPU_PINNING', 'BB_P_CPUS', 'BB_E_CPUS',
             'BB_RENDER_RES', 'BB_OUTPUT_RES'}
    lines = ['# Generated by cachyborne-setup. Source only from the prepared profile.',
             'set -euo pipefail']
    # The JSON path is immutable, even if another setup publishes while we start.
    diagnostic = ['python3', str(ROOT / 'launcher/cachyborne.py'), 'launch', '--profile', str(out / 'game_profile.json')]
    lines += ['if [[ ${1:-} == --dry-run || ${1:-} == --stage || ${1:-} == --help ]]; then',
              '    exec ' + shlex.join(diagnostic) + ' "$@"', 'fi']
    for name in ARTIFACTS:
        lines += ['[[ -s ' + quote(str(out / name)) + ' ]] || { echo "Missing prepared artifact; run setup." >&2; exit 1; }']
    lines += ['[[ -x ' + quote(profile['probe']) + ' && -f ' + quote(str(Path(profile['game']) / 'eboot.bin')) +
              ' ]] || { echo "Game/probe unavailable; run setup." >&2; exit 1; }']
    for key, value in env.items():
        assignment = 'export ' + key + '=' + quote(value)
        lines += [assignment if key in fixed else 'if [[ ! -v ' + key + ' ]]; then ' + assignment + '; fi']
    for key, tool in (('gamemode', 'gamemoderun'), ('gamescope', 'gamescope')):
        if profile['settings'][key]:
            lines += ['command -v ' + tool + ' >/dev/null || { echo "Missing ' + tool + '" >&2; exit 1; }']
    scheduler = profile['settings']['scheduler']
    if scheduler != 'current':
        lines += ['active_scheduler=',
                  'if [[ -r /sys/kernel/sched_ext/root/ops ]]; then IFS= read -r active_scheduler < /sys/kernel/sched_ext/root/ops || true; fi',
                  '[[ $active_scheduler == *' + quote(scheduler.removeprefix('scx_')) + '* ]] || { echo "Required sched-ext scheduler is not active." >&2; exit 1; }']
    if profile['settings']['ramdisk']:
        target = RAM_ROOT / profile['generation']
        marker = target / '.cachyborne-complete'
        lines += ['ram_source=',
                  'if [[ -O ' + quote(str(RAM_ROOT)) + ' && ! -L ' + quote(str(RAM_ROOT)) + ' && -O ' + quote(str(target)) + ' && ! -L ' + quote(str(target)) + ' && -f ' + quote(str(marker)) + ' && ! -L ' + quote(str(marker)) + ' ]]; then IFS= read -r ram_source < ' + quote(str(marker)) + ' || true; fi',
                  'if [[ $ram_source != ' + quote(profile['game']) + ' ]]; then',
                  '    ' + shlex.join(diagnostic + ['--stage']), 'fi']
        # Change only the app0 argument, not any arbitrary equal path in a wrapper.
        command[command.index('--app0') + 1] = str(target)
    lines += ['cd -- ' + quote(str(ROOT)), '[[ ${1:-} != -- ]] || shift', 'exec ' + shlex.join(command) + ' "$@"']
    (out / 'launch.sh').write_text('\n'.join(lines) + '\n')
    CONFIG.mkdir(parents=True, exist_ok=True, mode=0o700)
    temporary = CONFIG / ('.game-profile-' + uuid.uuid4().hex)
    try:
        temporary.symlink_to(out / 'game_profile.json')
        os.replace(temporary, CONFIG / 'game_profile.json')
    finally:
        temporary.unlink(missing_ok=True)


def check_runtime_requirements(profile):
    s = profile['settings']
    for key, tool in (('gamemode', 'gamemoderun'), ('gamescope', 'gamescope')):
        if s[key] and not shutil.which(tool):
            raise RuntimeError(f'Missing optional wrapper: {tool}')
    scheduler = s['scheduler']
    if scheduler != 'current':
        active = Path('/sys/kernel/sched_ext/root/ops')
        if not active.exists() or scheduler.removeprefix('scx_') not in active.read_text():
            raise RuntimeError(f'Enable {scheduler} through CachyOS scx-manager first, or select current scheduler.')


def desktop(on_desktop=False):
    directory = Path(os.environ.get('XDG_DATA_HOME', Path.home() / '.local/share')) / 'applications'
    if on_desktop:
        if shutil.which('xdg-user-dir'):
            directory = Path(subprocess.check_output(['xdg-user-dir', 'DESKTOP'], text=True).strip())
        else:
            directory = Path.home() / 'Desktop'
    directory.mkdir(parents=True, exist_ok=True)
    executable = str(ROOT / 'cachyborne-launch.sh')
    # Desktop Exec quoting is not shell quoting. Escape reserved characters.
    quoted = executable.replace('\\', '\\\\').replace('"', '\\"').replace('`', '\\`').replace('$', '\\$').replace('%', '%%')
    path = directory / 'cachyborne-play.desktop'
    path.write_text('[Desktop Entry]\nType=Application\nName=Cachyborne — Bloodborne\n'
                    f'Exec="{quoted}"\nIcon=applications-games\nTerminal=false\nCategories=Game;\n')
    path.chmod(0o755)
    print(path)
    return path


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='action', required=True)
    prepare = commands.add_parser('setup')
    prepare.add_argument('game', nargs='?')
    prepare.add_argument('--gui', action='store_true')
    prepare.add_argument('--fps', choices=['30', '60', '90', '120', '144', '165', 'uncap'])
    prepare.add_argument('--upscaler', choices=['off', 'fsr3', 'fsr411'])
    for key in ('render-res', 'output-res', 'fsr411-dir', 'mods-dir', 'mods-config', 'patches-dir', 'patches-config'):
        prepare.add_argument('--' + key)
    for key in ('ramdisk', 'gamemode', 'gamescope', 'pinning'):
        prepare.add_argument('--' + key, action=argparse.BooleanOptionalAction, default=None)
    launch = commands.add_parser('launch')
    launch.add_argument('--profile', type=Path, help=argparse.SUPPRESS)
    launch.add_argument('--dry-run', action='store_true')
    launch.add_argument('--stage', action='store_true', help='Preload RAM only; do not start the game')
    launch.add_argument('probe_args', nargs=argparse.REMAINDER)
    shortcut = commands.add_parser('desktop')
    shortcut.add_argument('--desktop', action='store_true', help='Also copy the shortcut to the XDG desktop directory')
    args = parser.parse_args(argv)
    try:
        if args.action == 'setup':
            if args.gui:
                os.execv(sys.executable, [sys.executable, str(ROOT / 'launcher/cachyborne_gui.py'), '--setup'])
            if not args.game:
                parser.error('setup requires GAME_DIR or --gui')
            settings = preferences()
            settings.update({k: v for k, v in vars(args).items() if k in DEFAULTS and v is not None})
            setup(args.game, settings)
        elif args.action == 'desktop':
            desktop()
            if args.desktop:
                desktop(on_desktop=True)
        else:
            profile = load_profile(args.profile)
            app0 = profile['game']
            if args.stage or (profile['settings']['ramdisk'] and not args.dry_run):
                app0 = stage(profile)
            if args.stage:
                return 0
            extra = args.probe_args
            if extra[:1] == ['--']:
                extra = extra[1:]
            command, env = launch_command(profile, app0, extra)
            if args.dry_run:
                print(json.dumps(dict(command=command, environment={k: v for k, v in env.items()
                      if k.startswith(('BB_', 'RADV_', 'MESA_', 'AMD_VULKAN', 'VK_'))}), indent=2))
                return 0
            check_runtime_requirements(profile)
            os.chdir(ROOT)
            os.execvpe(command[0], command, env)
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f'Cachyborne: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
