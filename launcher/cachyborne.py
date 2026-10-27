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
DEFAULTS = dict(voice_language='eng', memory_profile='16gb', pinning=True, p_cpus='0-11', e_cpus='12-19', ramdisk=False,
                gamemode=False, radv=True, gamescope=False, hdr=False, vrr=False,
                gamescope_fsr=False, width=2560, height=1440, fps='144', fps_limit=0,
                upscaler='fsr4', output_res='2560x1440', render_res='1706x960',
                fullscreen=True, present_mode='mailbox', audio=True, gamepad='',
                scheduler='current', fsr4_dir='', fsr411_dll='', fsr411_loader='', dlss_library='', fsr411_dir='', mods_dir='', mods_enabled=True, mods_config='',
                patches_dir='', patches_config='', user_dir='', runtime_env={}, original_settings={},
                runtime_ini={'preset': '1', 'sharpen': '1', 'sharpness': '0.35', 'object_motion': '1',
                             'show_fps': '1', 'menu_language': 'es', 'live_resolution': '0', 'effect_chromatic_aberration': '0', 'effect_dof': '0',
                             'effect_motion_blur': '0', 'skip_intro': '1'})
ICON_URL = ('https://image.api.playstation.com/vulcan/img/rnd/202010/2614/'
            'O2Z66UWrZH8zcejxopwWxhGu.png?w=512&h=512')
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
    profile = read_json(CONFIG / 'game_profile.json', {})
    prepared = profile.get('settings', {})
    personal = read_json(CONFIG / 'defaults.json', {})
    draft = read_json(CONFIG / 'settings.json', {})
    active_ini = {}
    path = Path(profile.get('prepared', DATA / 'missing')) / 'bbport.ini'
    if path.is_file():
        for line in path.read_text().splitlines():
            if '=' in line and not line.lstrip().startswith(('#', ';')):
                key, value = line.split('=', 1)
                active_ini[key.strip()] = value.strip()
    result = DEFAULTS | personal | prepared | draft
    draft_path = CONFIG / 'settings.json'
    active_is_newer = path.is_file() and (not draft_path.is_file() or path.stat().st_mtime_ns > draft_path.stat().st_mtime_ns)
    changes = (draft.get('runtime_ini', {}) | active_ini) if active_is_newer else (active_ini | draft.get('runtime_ini', {}))
    result['runtime_ini'] = dict(DEFAULTS['runtime_ini']) | personal.get('runtime_ini', {}) | prepared.get('runtime_ini', {}) | changes
    for key in ('upscaler', 'output_res'):
        if key in active_ini and (active_is_newer or key not in draft):
            result[key] = active_ini[key]
    return result


def save_preferences(settings, as_default=False):
    """Persist intent independently of game ingestion or optional runtime dependencies."""
    import copy
    snapshot = copy.deepcopy(settings)
    atomic_json(CONFIG / 'settings.json', snapshot)
    if as_default:
        atomic_json(CONFIG / 'defaults.json', snapshot)
    return snapshot


def performance_settings(current=None, target='144'):
    """A target, not a frame-rate guarantee; preserve bindings, mods and user directories."""
    import copy
    settings = copy.deepcopy(DEFAULTS if current is None else DEFAULTS | current)
    settings['runtime_ini'] = dict(DEFAULTS['runtime_ini']) | settings.get('runtime_ini', {})
    settings.update(fps=target, fps_limit=int(target), pinning=True, ramdisk=False, radv=True)
    settings.update(upscaler='fsr4', output_res='2560x1440', render_res='1706x960',
                    width=2560, height=1440, p_cpus='0-11', e_cpus='12-19',
                    voice_language='eng', present_mode='mailbox')
    settings['original_settings'] = dict(settings.get('original_settings', {})) | dict(language='20', ui_language='es', memory_model='auto', as_0_3=False)
    settings['runtime_ini'].update(menu_language='es', preset='1', effect_chromatic_aberration='0', effect_dof='0', effect_motion_blur='0', live_resolution='0')
    settings['runtime_ini'].update(upscaler=settings['upscaler'], output_res=settings['output_res'])
    # Clear conflicting old editor overrides when explicitly applying a recommended preset.
    for key in ('BB_FPS', 'BB_FPS_LIMIT', 'BB_UPSCALER', 'BB_UPSCALE_PRESET', 'BB_RENDER_RES',
                'BB_OUTPUT_RES', 'BB_PC_MODEL', 'BB_GUEST_IN_PLACE', 'BB_AS_0_3', 'BB_VOICE_LANGUAGE',
                'BB_LANGUAGE', 'BB_MENU_LANGUAGE', 'BB_PRESENT_MODE', 'BB_P_CPUS', 'BB_E_CPUS'):
        settings['runtime_env'].pop(key, None)
    return settings


def validate_settings(s):
    import re
    for key in ('p_cpus', 'e_cpus'):
        if not re.fullmatch(r'\d+(?:-\d+)?(?:,\d+(?:-\d+)?)*', s[key]):
            raise ValueError(f'Invalid CPU list: {key}')
        for span in s[key].split(','):
            bounds = list(map(int, span.split('-')))
            if not (0 <= bounds[0] <= bounds[-1] < 1024):
                raise ValueError(f'Invalid CPU range: {key}={span}')
    from cachyborne_options import VOICE_LANGUAGES
    if s.get('voice_language', 'eng') not in dict(VOICE_LANGUAGES):
        raise ValueError('Idioma de voces no compatible')
    if s['memory_profile'] != '16gb':
        raise ValueError('Unsupported memory profile; expected 16gb')
    if s['fps'] not in ('30', '60', '90', '120', '144', '165', 'uncap'):
        raise ValueError('Unsupported FPS')
    if not 0 <= int(s.get('fps_limit', 0)) <= 1000:
        raise ValueError('Invalid custom FPS limit')
    for key, value in s.get('runtime_env', {}).items():
        if not re.fullmatch(r'[A-Za-z_][A-Za-z_0-9]*', key) or not isinstance(value, str) or '\0' in value:
            raise ValueError(f'Invalid runtime environment variable: {key}')
    for key, value in s.get('runtime_ini', {}).items():
        if not re.fullmatch(r'[A-Za-z_][A-Za-z_0-9.]*', key) or any(c in str(value) for c in '\r\n\0'):
            raise ValueError(f'Invalid game setting: {key}')
    if s['upscaler'] not in ('off', 'fsr3', 'fsr4', 'fsr411', 'dlss', 'taa'):
        raise ValueError('Unsupported upscaler')
    for key in ('output_res', 'render_res'):
        match = re.fullmatch(r'(\d+)x(\d+)', s[key])
        if not match or not (320 <= int(match[1]) <= 16384 and 200 <= int(match[2]) <= 16384):
            raise ValueError(f'Invalid resolution: {key}')
    if s['present_mode'] not in ('fifo', 'fiforelaxed', 'mailbox', 'immediate'):
        raise ValueError('Unsupported presentation mode')
    if s['scheduler'] not in ('current', 'scx_lavd', 'scx_bpfland'):
        raise ValueError('Unsupported scheduler')
    for key in ('width', 'height'):
        if not 200 <= int(s[key]) <= 16384:
            raise ValueError(f'Invalid Gamescope {key}')


def prepare_online_settings(settings, user):
    env = settings.get('runtime_env', {})
    if env.get('BB_ONLINE') != '1':
        return
    module = ROOT / 'out/gpu/libbbnet20.so'
    if not module.is_file():
        raise RuntimeError('The online module must be built before enabling online play.')
    sys.path.insert(0, str(ROOT / 'scripts'))
    import online
    if env.get('BB_NET_BACKEND') == 'thd':
        if not env.get('BB_THD_TOKEN') or not env.get('BB_THD_ONLINE_ID'):
            raise RuntimeError("Sign in to The Hunter's Dream in Online y cuentas first.")
        path, base = online.write_thd_overrides(user, env.get('BB_COMMUNITY_SERVER'))
        env.update(BB_THD_SERVER=f'{base}:18671', BB_THD_AUTH=base)
    else:
        custom = user / 'host_overrides.custom.json'
        path = custom if custom.is_file() else user / 'host_overrides.json'
        if not custom.is_file():
            community = online.community_address(env.get('BB_COMMUNITY_SERVER'))
            host = community.split('://', 1)[1]
            overrides = {online.GAME_SERVER: community}
            webapi = env.get('BB_SHADNET_WEBAPI', '')
            if webapi:
                overrides.update({f'{scheme}://{host}:{online.COMMUNITY_API_PORT}/summon_messenger': webapi
                                  for scheme in ('http', 'https')})
            user.mkdir(parents=True, exist_ok=True)
            atomic_json(path, overrides)
    env.update(SHADPS4_HTTP_HOST_OVERRIDES_JSON=str(path), BB_NET_MODULE=str(module), BB_NET_GEN='20')


def shell_logging_lines(profile):
    logs = shlex.quote(str(DATA / 'logs'))
    return ['if [[ ${BB_SAVE_LOG:-0} == 1 ]]; then',
            f'    logs={logs}; mkdir -p -- "$logs"',
            '    printf -v stamp "%(%Y%m%d_%H%M%S)T" -1; stamp="${stamp}_$$"',
            '    export BB_FRAME_STATS=1 BB_FRAME_LOG="$logs/$stamp.frames.csv" BB_READBACK_LOG="$logs/$stamp.readbacks.csv"',
            '    exec >> "$logs/$stamp.log" 2>&1', 'fi']


def configure_logging(profile, env):
    if env.get('BB_SAVE_LOG') != '1':
        return
    logs = DATA / 'logs'
    logs.mkdir(parents=True, exist_ok=True)
    stamp = time.strftime('%Y%m%d_%H%M%S') + f'_{os.getpid()}'
    env.update(BB_FRAME_STATS='1', BB_FRAME_LOG=str(logs / (stamp + '.frames.csv')),
               BB_READBACK_LOG=str(logs / (stamp + '.readbacks.csv')))
    with (logs / (stamp + '.log')).open('a') as stream:
        os.dup2(stream.fileno(), 1)
        os.dup2(stream.fileno(), 2)


def public_environment(env):
    return {key: ('[redacted]' if any(secret in key.upper() for secret in ('TOKEN', 'PASSWORD', 'AUTH')) else value)
            for key, value in env.items() if key.startswith(('BB_', 'RADV_', 'MESA_', 'AMD_VULKAN', 'VK_'))}


def direct_memory_mb(settings):
    width, height = map(int, settings['render_res'].split('x'))
    return 5056 if width * height <= 1920 * 1080 else 9152


def amd_gtt_budget(sysfs=Path('/sys/class/drm')):
    budgets = []
    for card in sysfs.glob('card[0-9]*'):
        try:
            if (card / 'device/vendor').read_text().strip() == '0x1002':
                budgets.append((int((card / 'device/mem_info_gtt_total').read_text()), card / 'device/mem_info_gtt_total'))
        except (OSError, ValueError): pass
    return max(budgets, default=(0, None))


def check_memory_budget(settings):
    if settings.get('memory_profile') == '16gb' and direct_memory_mb(settings) > 5056:
        raise ValueError('El perfil de 16 GB admite hasta 1920x1080 internos (o 1916x1078); usa FSR para salida 1440p/4K. El pool de 9152 MiB excede el presupuesto seguro.')
    env = settings.get('runtime_env', {})
    if env.get('BB_GUEST_VRAM') == '1' or env.get('BB_PC_MODEL') == '0' or env.get('BB_GUEST_IN_PLACE') == '0': return
    budget, _ = amd_gtt_budget()
    required = (direct_memory_mb(settings) + 256 + 512) * 1024**2
    if budget and required > budget:
        raise ValueError(f'La resolución interna requiere un pool GPU de {direct_memory_mb(settings)} MiB, '
                         f'pero GTT dispone de {budget // 1024**2} MiB. '
                         'Usa como máximo 1916x1078 internos con salida 2560x1440 y FSR 4; '
                         '1440p nativo supera el presupuesto de este perfil de memoria.')


def preparation_status(settings):
    """Cheap preliminary checks for the GUI; setup still validates the full dump."""
    game = Path(settings.get('game') or '').expanduser()
    for relative in ('eboot.bin', 'sce_sys/param.sfo', 'dvdroot_ps4'):
        if not (game / relative).exists():
            return f'Falta {relative}. Selecciona un dump descifrado con la actualización 1.09.'
    from cachyborne_options import available_languages, fsr4_problem
    if not available_languages(game):
        return 'Faltan archivos completos de textos en dvdroot_ps4/msg.'
    try:
        check_memory_budget(settings)
    except ValueError as error:
        return str(error)
    if settings.get('upscaler') == 'fsr4':
        assets = settings.get('fsr4_dir') or settings.get('runtime_env', {}).get('BB_FSR4_DIR') or ROOT / 'fsr4_shaders'
        issue = fsr4_problem(Path(assets).expanduser(), settings['output_res'], settings.get('runtime_ini', {}).get('preset', '1'))
        if issue:
            return issue + '. Al preparar se usará FSR 3.1.'
    return 'Archivos básicos e idiomas disponibles. Preparar verificará identidad y todos los assets DCX.'


def setup(game, settings=None):
    import copy
    s = copy.deepcopy(preferences() if settings is None else settings)
    if s.get('original_settings'):
        from cachyborne_options import reconcile
        s = reconcile(s)
    validate_settings(s)
    setup_env = os.environ | s.get('runtime_env', {})
    if setup_env.get('BB_SKIP_GAME_CHECK') == '1':
        raise RuntimeError('Setup requires validation; unset BB_SKIP_GAME_CHECK.')
    if s['upscaler'] == 'fsr4':
        from cachyborne_options import fsr4_problem
        assets = Path(s.get('fsr4_dir') or s.get('runtime_env', {}).get('BB_FSR4_DIR') or ROOT / 'fsr4_shaders').expanduser().resolve()
        issue = fsr4_problem(assets, s['output_res'], s.get('runtime_ini', {}).get('preset', '1'))
        if issue:
            print(issue + '. Se usará FSR 3.1; instala assets con tools/fetch_fsr4_assets.sh para FSR 4.', file=sys.stderr)
            s['upscaler'] = 'fsr3'
            s.setdefault('runtime_ini', {})['upscaler'] = 'fsr3'
            s.setdefault('runtime_env', {}).pop('BB_UPSCALER', None)
        else:
            s.setdefault('runtime_env', {})['BB_FSR4_DIR'] = str(assets)
    game = Path(game).expanduser().resolve(strict=True)
    if not (ROOT / 'out/bb-probe').is_file():
        raise RuntimeError('Build first: bash build.sh')
    sys.path.insert(0, str(ROOT / 'scripts'))
    import game_check
    import mods
    mod_layers = mods.layers_for_game(game, s['mods_dir'] or DATA / 'mods',
                                     s.get('mod_selection', Path(s['mods_config']).expanduser() if s['mods_config'] else CONFIG / 'mods.json'),
                                     s.get('mods_enabled', True))
    from cachyborne_options import validate_game_language
    validate_game_language(s, game)
    setup_env = os.environ | s.get('runtime_env', {})
    check_memory_budget(s)
    image_hash = game_check.image_sha256(game)
    problem = game_check.problem(game, image_hash)
    if problem:
        raise RuntimeError(game_check.explain(*problem))
    subprocess.run([sys.executable, str(ROOT / 'scripts/game_check.py'), str(game)], check=True, env=setup_env)
    live = str(s.get('runtime_ini', {}).get('live_resolution', '0'))
    if live == 'auto':
        probe = ROOT / 'out/bb-gpu-capabilities'
        detected = subprocess.check_output([str(probe), '--live-resolution'], text=True, env=setup_env).strip()
        s.setdefault('runtime_ini', {})['live_resolution'] = '1' if detected == '1' else '0'
    if s['upscaler'] == 'taa':
        s.setdefault('runtime_ini', {})['live_resolution'] = '1'
    if s['upscaler'] == 'fsr411':
        assets = Path(s['fsr411_dir'] or ROOT / 'fsr4_411').expanduser().resolve()
        # The full replay asset set is checked by the renderer; do not advertise absent assets.
        from bbport_assets import fsr411_problem
        issue = fsr411_problem(assets, s['output_res'], int(s.get('runtime_ini', {}).get('preset', '1')))
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
        if mod_layers:
            game = mods.build_overlay(game, out, mod_layers)
            # Persist this generation's symlink view. Sources must remain in place until next setup.
            problem = game_check.problem(game)
            if problem:
                raise RuntimeError(game_check.explain(*problem))
        ini = out / 'bbport.ini'
        ini_values = dict(s.get('runtime_ini', {}))
        ini_values.update(upscaler=s['upscaler'], output_res=s['output_res'])
        ini_values.setdefault('live_resolution', '0')
        ini.write_text(''.join(f'{key}={value}\n' for key, value in ini_values.items()))
        for script in ('prepare.py', 'link_libc.py', 'link_modules.py', 'content_profile.py'):
            subprocess.run([sys.executable, str(ROOT / 'scripts' / script), str(game), '--out', str(out)],
                           cwd=ROOT, check=True, env=setup_env)
        fps = s['fps'] if s['fps'] in ('30', '60', '90') else 'uncap'
        patch_args = []
        if s['patches_dir']:
            patch_args += ['--patches-dir', str(Path(s['patches_dir']).expanduser())]
        if 'patch_selection' in s:
            selection_file = out / 'selected-patches.json'
            atomic_json(selection_file, s['patch_selection'])
            patch_args += ['--patches-config', str(selection_file)]
        elif s['patches_config']:
            patch_args += ['--patches-config', str(Path(s['patches_config']).expanduser())]
        subprocess.run([sys.executable, str(ROOT / 'scripts/patches.py'), '--out', str(out),
                        '--game-dir', str(game), '--fps', fps, '--settings', str(ini),
                        '--render-res', '' if str(ini_values['live_resolution']) == '1' else s['render_res'], '--output-res', s['output_res'], *patch_args], cwd=ROOT, check=True, env=setup_env)
        for name in ARTIFACTS:
            if not (out / name).is_file() or not (out / name).stat().st_size:
                raise RuntimeError(f'Setup did not create {name}')
        user = Path(s.get('user_dir') or DATA / 'user').expanduser().resolve()
        user.mkdir(parents=True, exist_ok=True)
        CACHE.mkdir(parents=True, exist_ok=True, mode=0o700)
        prepare_online_settings(s, user)
        s['runtime_ini'] = ini_values
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
    env.update(s.get('runtime_env', {}))
    out = Path(profile['prepared'])
    direct_mb = str(direct_memory_mb(s))
    values = dict(BB_CONFIG=str(out / 'bbport.ini'), BB_GPU_USER_DIR=profile['user'],
                  MESA_SHADER_CACHE_DIR=str(Path(profile['cache']) / 'mesa'), MESA_SHADER_CACHE_MAX_SIZE='4G',
                  BB_CPU_PINNING='1' if s['pinning'] else '0', BB_P_CPUS=s['p_cpus'], BB_E_CPUS=s['e_cpus'],
                  BB_RENDER_RES=s['render_res'], BB_OUTPUT_RES=s['output_res'],
                  BB_FULLSCREEN='1' if s['fullscreen'] else '0', BB_HDR='1' if s['hdr'] else '0', BB_PRESENT_MODE={'fiforelaxed': 'FifoRelaxed'}.get(s['present_mode'], s['present_mode'].capitalize()),
                  BB_FPS=s['fps'] if s['fps'] in ('30', '60', '90') else 'uncap', BB_FPS_LIMIT=str(s.get('fps_limit') or ('0' if s['fps'] == 'uncap' else s['fps'])),
                  BB_VBLANK_HZ='60' if s['fps'] == '30' else '1000', BB_DMEM_MB=direct_mb,
                  BB_DMEM_EXTRA_MB='256', BB_RENDER_RESOURCE_CACHE_MB='256', BB_MEMORY_WILLNEED='0', BB_ASYNC_PIPELINE_THREADS='2',
                  BB_PC_MODEL='1', BB_GUEST_IN_PLACE='1', BB_LAYER_MEMORY='1', BB_PREUPLOAD='1',
                  BB_ASYNC_SUBMIT='1', BB_VK_RECORD_THREADS='3',
                  BB_VOICE_LANGUAGE=s.get('voice_language', 'eng'),
                  BB_LANGUAGE=str(s.get('original_settings', {}).get('language', '20')), BB_MENU_LANGUAGE='es',
                  BB_ICON_BMP=str(ROOT / 'assets/icons/cachyborne.bmp'), SDL_APP_ID='cachyborne-play',
                  BB_COPY_GPU_BUFFERS='1', BB_GPU_WRITE_TWINS='1', BB_GPU_WRITE_TWINS_MAX='65536',
                  BB_UFFD='0', BB_AUDIO='auto' if s['audio'] else 'none', BB_GAMEPAD=s['gamepad'])
    if str(s.get('runtime_ini', {}).get('live_resolution', '0')) == '1':
        values['BB_RENDER_RES'] = values['BB_OUTPUT_RES'] = ''
    if env.get('BB_PC_MODEL') == '0' or env.get('BB_AS_0_3') == '1':
        values['BB_GUEST_IN_PLACE'] = '0'
    if env.get('BB_AS_0_3') == '1':
        values.update(BB_PC_MODEL='0', BB_HOST_COPY_WAITS='all', BB_PRODUCER_CHECK='1')
    if s['fsr411_dir']:
        values['BB_FSR411_DIR'] = s['fsr411_dir']
    if s['radv']:
        values.update(AMD_VULKAN_ICD='RADV', RADV_PERFTEST='sam', RADV_TEX_ANISO='16',
                      VK_LOADER_LAYERS_DISABLE='' if env.get('MANGOHUD') == '1' else '~implicit~')
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
    gtt_budget, gtt_file = amd_gtt_budget()
    if gtt_budget:
        required = (direct_memory_mb(profile['settings']) + 256 + 512) * 1024**2
        lines += ['if [[ ${BB_GUEST_VRAM:-0} != 1 && ${BB_PC_MODEL:-1} != 0 && ${BB_GUEST_IN_PLACE:-1} != 0 && -r ' + quote(str(gtt_file)) + ' ]]; then',
                  '    gtt_budget=',
                  '    IFS= read -r gtt_budget < ' + quote(str(gtt_file)) + ' || true',
                  '    [[ $gtt_budget =~ ^[0-9]+$ && $gtt_budget -ge ' + str(required) + ' ]] || { echo "Insufficient AMD GTT budget; use 1916x1078 internal resolution and run setup." >&2; exit 1; }',
                  'fi']
    lines += shell_logging_lines(profile)
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
    check_memory_budget(s)
    for key, tool in (('gamemode', 'gamemoderun'), ('gamescope', 'gamescope')):
        if s[key] and not shutil.which(tool):
            raise RuntimeError(f'Missing optional wrapper: {tool}')
    scheduler = s['scheduler']
    if scheduler != 'current':
        active = Path('/sys/kernel/sched_ext/root/ops')
        if not active.exists() or scheduler.removeprefix('scx_') not in active.read_text():
            raise RuntimeError(f'Enable {scheduler} through CachyOS scx-manager first, or select current scheduler.')


def icon_path():
    data_home = Path(os.environ.get('XDG_DATA_HOME', Path.home() / '.local/share'))
    return data_home / 'icons/hicolor/512x512/apps/cachyborne.png'


def install_bundled_icon():
    """Install the original square icon offline; no game or third-party artwork required."""
    path = icon_path()
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(dir=path.parent, prefix='.cachyborne-icon-')
    try:
        with os.fdopen(fd, 'wb') as stream:
            stream.write((ROOT / 'assets/icons/cachyborne.png').read_bytes())
        os.chmod(temporary, 0o644)
        os.replace(temporary, path)
    finally:
        Path(temporary).unlink(missing_ok=True)
    scalable = path.parents[2] / 'scalable/apps/cachyborne.svg'
    scalable.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(ROOT / 'assets/icons/cachyborne.svg', scalable)
    return path


def download_desktop_icon():
    """Download official artwork for the user's local shortcuts; never bundle it."""
    import urllib.request
    request = urllib.request.Request(ICON_URL.split('?', 1)[0], headers={'User-Agent': 'Cachyborne/1.0'})
    with urllib.request.urlopen(request, timeout=20) as response:
        data = response.read(5 * 1024**2 + 1)
    # Sony's CDN may serve JPEG even when the source URL ends in .png.
    if len(data) > 5 * 1024**2 or not (data.startswith(b'\x89PNG\r\n\x1a\n') or data.startswith(b'\xff\xd8\xff')):
        raise ValueError('Invalid downloaded icon: expected a PNG or JPEG up to 5 MiB.')
    import gi
    gi.require_version('GdkPixbuf', '2.0')
    from gi.repository import GdkPixbuf, GLib
    try:
        loader = GdkPixbuf.PixbufLoader.new()
        loader.write(data)
        loader.close()
        pixbuf = loader.get_pixbuf()
        if pixbuf is None:
            raise ValueError('Downloaded icon has no image data.')
        # Contain the source instead of asking the CDN to stretch its dimensions.
        ratio = min(512 / pixbuf.get_width(), 512 / pixbuf.get_height())
        width, height = max(1, round(pixbuf.get_width() * ratio)), max(1, round(pixbuf.get_height() * ratio))
        scaled = pixbuf.scale_simple(width, height, GdkPixbuf.InterpType.HYPER)
        square = GdkPixbuf.Pixbuf.new(GdkPixbuf.Colorspace.RGB, True, 8, 512, 512)
        square.fill(0x101018ff)
        scaled.copy_area(0, 0, width, height, square, (512 - width) // 2, (512 - height) // 2)
        success, data = square.save_to_bufferv('png', [], [])
        if not success:
            raise ValueError('Could not encode the desktop icon.')
    except GLib.Error as error:
        raise ValueError(f'Invalid downloaded image: {error}') from error
    path = icon_path()
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(dir=path.parent, prefix='.cachyborne-icon-')
    try:
        with os.fdopen(fd, 'wb') as stream:
            stream.write(data)
        os.chmod(temporary, 0o644)
        os.replace(temporary, path)
    finally:
        Path(temporary).unlink(missing_ok=True)
    return path


def desktop_value(value):
    return str(value).replace('\\', '\\\\').replace('\n', '\\n').replace('\r', '\\r').replace('\t', '\\t')


def desktop_exec(executable):
    # First escape the quoted Exec argument, then escape the desktop key's value.
    argument = str(executable).replace('\\', '\\\\').replace('"', '\\"').replace('`', '\\`').replace('$', '\\$').replace('%', '%%')
    return desktop_value('"' + argument + '"')


def desktop(on_desktop=False, configuration=False):
    directory = Path(os.environ.get('XDG_DATA_HOME', Path.home() / '.local/share')) / 'applications'
    if on_desktop:
        if shutil.which('xdg-user-dir'):
            location = subprocess.check_output(['xdg-user-dir', 'DESKTOP'], text=True).strip()
            if not location or not Path(location).is_absolute():
                raise ValueError('Invalid XDG desktop directory')
            directory = Path(location)
        else:
            directory = Path.home() / 'Desktop'
    directory.mkdir(parents=True, exist_ok=True)
    executable = ROOT / ('cachyborne-gui' if configuration else 'cachyborne-launch.sh')
    path = directory / ('cachyborne.desktop' if configuration else 'cachyborne-play.desktop')
    name = 'Cachyborne — Configuración' if configuration else 'Bloodborne — Cachyborne'
    icon = str(icon_path()) if icon_path().is_file() else str(ROOT / 'assets/icons/cachyborne.png')
    categories = 'Settings;' if configuration else 'Game;'
    path.write_text('[Desktop Entry]\nType=Application\n' +
                    f'Name={name}\nExec={desktop_exec(executable)}\nIcon={desktop_value(icon)}\n' +
                    f'Terminal=false\nCategories={categories}\nStartupNotify=true\n' +
                    f'StartupWMClass={"org.cachyborne.Preferences" if configuration else "cachyborne-play"}\n')
    path.chmod(0o755)
    print(path)
    return path


def install_shortcuts(on_desktop=False, download_icon=False):
    if not download_icon:
        install_bundled_icon()
    if download_icon:
        try:
            download_desktop_icon()
        except (OSError, ValueError, ImportError) as error:
            print(f'Cachyborne: icon download failed; keeping the existing icon: {error}', file=sys.stderr)
    entries = [desktop(configuration=config) for config in (False, True)]
    if on_desktop:
        entries += [desktop(on_desktop=True, configuration=config) for config in (False, True)]
    return entries


def cancellable_setup(game, settings):
    import signal
    def interrupted(*_):
        raise KeyboardInterrupt
    previous = signal.signal(signal.SIGTERM, interrupted)
    try:
        return setup(game, settings)
    finally:
        signal.signal(signal.SIGTERM, previous)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='action', required=True)
    prepare = commands.add_parser('setup')
    prepare.add_argument('game', nargs='?')
    prepare.add_argument('--gui', action='store_true')
    prepare.add_argument('--performance-profile', choices=['144', '60'])
    prepare.add_argument('--fps', choices=['30', '60', '90', '120', '144', '165', 'uncap'])
    prepare.add_argument('--upscaler', choices=['off', 'fsr3', 'fsr4', 'fsr411', 'taa', 'dlss'])
    from cachyborne_options import VOICE_LANGUAGES
    prepare.add_argument('--voice-language', choices=list(dict(VOICE_LANGUAGES)))
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
    shortcut.add_argument('--download-icon', action='store_true', help='Download official Bloodborne artwork for local shortcuts')
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
            if args.performance_profile:
                settings = performance_settings(settings, args.performance_profile)
            cancellable_setup(args.game, settings)
        elif args.action == 'desktop':
            install_shortcuts(on_desktop=args.desktop, download_icon=args.download_icon)
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
                print(json.dumps(dict(command=command, environment=public_environment(env)), indent=2))
                return 0
            check_runtime_requirements(profile)
            configure_logging(profile, env)
            os.chdir(ROOT)
            os.execvpe(command[0], command, env)
    except KeyboardInterrupt:
        print('Cachyborne: preparación cancelada; perfil anterior conservado.', file=sys.stderr)
        return 130
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f'Cachyborne: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
