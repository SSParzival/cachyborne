"""Adapter for the complete upstream GTK editor and Cachyborne's prepared profiles."""
import copy
import os
from pathlib import Path

import cachyborne as core


def configure_module():
    import bbport_launcher as editor
    # Keep upstream tools/preferences writable and isolated from the source checkout.
    editor.DATA_DIR = core.DATA / 'editor'
    editor.CONFIG_DIR = core.CONFIG / 'editor'
    editor.CONFIG_FILE = editor.CONFIG_DIR / 'settings.json'
    editor.DATA_DIR.mkdir(parents=True, exist_ok=True)
    return editor


def seed_editor(settings):
    editor = configure_module()
    stored = editor.load_settings()
    stored.update(settings.get('original_settings', {}))
    stored.update(game_dir=settings.get('game', ''), user_dir=settings.get('user_dir', '') or str(core.DATA / 'user'),
                  fps_mode=settings['fps'] if settings['fps'] in ('30', '60', '90', 'uncap') else 'uncap',
                  fps_limit=0 if settings['fps'] == 'uncap' else int(settings['fps']),
                  fullscreen=settings['fullscreen'], present_mode=settings['present_mode'].capitalize(),
                  gamepad=settings['gamepad'], mods_dir=settings['mods_dir'], patches_dir=settings['patches_dir'])
    stored['ui_language'] = stored.get('ui_language') or 'en'
    editor.save_settings(stored)
    ini = dict(editor.INI_DEFAULTS)
    ini.update(settings.get('runtime_ini', {}))
    ini.update(upscaler=settings['upscaler'], output_res=settings['output_res'])
    editor.save_ini(ini, [])
    return editor


def import_editor(settings, original, ini):
    """Map every upstream INI key and launch environment; preserve CachyOS-only switches."""
    result = copy.deepcopy(settings)
    result.update(original_settings=copy.deepcopy(original), runtime_ini=dict(ini),
                  game=original['game_dir'], user_dir=original.get('user_dir', ''),
                  fullscreen=original['fullscreen'], hdr=original['hdr'],
                  present_mode=original['present_mode'].lower(), gamepad=original.get('gamepad', ''),
                  mods_dir=original.get('mods_dir', ''), mods_enabled=original.get('mods_enabled', True),
                  patches_dir=original.get('patches_dir', ''), upscaler=ini.get('upscaler', 'fsr3'),
                  output_res=ini.get('output_res', '2560x1440'))
    mode, limit = original.get('fps_mode', 'uncap'), int(original.get('fps_limit', 0))
    result['fps'] = str(limit) if limit in (30, 60, 90, 120, 144, 165) else mode
    result['fps_limit'] = limit
    # Compute the fixed-resolution patch exactly as the upstream preset does.
    import patches
    sizes = patches.scaled_sizes(ini)
    result['render_res'] = f'{sizes[0][0]}x{sizes[0][1]}' if sizes else '1920x1080'
    if result['output_res'] == '1920x1080' or result['upscaler'] == 'taa':
        result['runtime_ini']['live_resolution'] = '1'
    editor = configure_module()
    environment = editor.game_environment(original)
    result['runtime_env'] = {key: value for key, value in environment.items()
                             if key not in os.environ or os.environ[key] != value}
    # These are explicit profile choices even when the same value was inherited.
    for key in ('BB_FPS', 'BB_FPS_LIMIT', 'BB_LANGUAGE', 'BB_PRESENT_MODE', 'BB_FULLSCREEN'):
        if key in environment:
            result['runtime_env'][key] = environment[key]
    for key in ('BB_GAME_DIR', 'BB_CONFIG', 'BB_USER_DIR', 'BB_MODS_DIR', 'BB_MODS_CONFIG',
                'BB_MODS_ENABLED', 'BB_PATCHES_DIR', 'BB_PATCHES_CONFIG'):
        result['runtime_env'].pop(key, None)
    result['mods_dir'] = str(Path(original.get('mods_dir') or editor.DATA_DIR / 'mods').expanduser())
    result['patches_dir'] = str(editor.patches_dir(original))
    result['mods_config'] = str(editor.DATA_DIR / 'mods.json')
    result['patches_config'] = str(editor.DATA_DIR / 'patches.json')
    if result['upscaler'] == 'fsr411':
        result['fsr411_dir'] = str(editor.DATA_DIR / 'fsr4_411')
    core.validate_settings(result)
    return result
