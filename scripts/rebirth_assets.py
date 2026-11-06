#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Prepare the altar's script and messages from the player's own 1.09 dump.

Uses the hash-checked BBHOST script rewrite; archive/message edits adapt
menu_assets.cpp at bdc5cdf8be5f37188da9c2368e92be444fdc04cb. No runtime altar
service is enabled here. Generated layers belong behind selected player mods.
"""
import argparse
from dataclasses import replace
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

import bnd4
import fmg
from world_assets import pack_layout, unpack_layout

TALK_PATH = Path('dvdroot_ps4/script/talk/m24_02_00_00.talkesdbnd.dcx')
SCRIPT_NAME = 't242307.esd'
SCRIPT_INPUT = 'e56163a1bb8f46ae59368160e522e11f970e2268f6d356b823e4d72a7c969ff1'
SCRIPT_OUTPUT = 'e726dec68e8d73c5d5b6ba1875fabd6a85320b6b14f95ca3d6729143a3c362b2'
TEXTS = {
    'en': {
        14000332: 'Rebirth in the Nightmare',
        14000333: 'The altar is silent.\nIt awaits one who bears a sacred heirloom,\na consciousness that is only asleep.',
        14000334: 'Rebirth could not be completed.\nSpend some Blood Echoes and try again.',
        14000335: 'Accept Rebirth',
        14000336: 'Undo Rebirth',
    },
    'es': {
        14000332: 'Renacer en la pesadilla',
        14000333: 'El altar permanece en silencio.\nAguarda a quien porte una reliquia sagrada,\nuna conciencia que sólo duerme.',
        14000334: 'No se ha podido completar el renacimiento.\nGasta algunos ecos de sangre y vuelve a intentarlo.',
        14000335: 'Aceptar el renacimiento',
        14000336: 'Deshacer el renacimiento',
    },
}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def rewrite_messages(packed, language):
    bundle = bnd4.read(unpack_layout(packed, max_size=bnd4.MAX_ARCHIVE))
    texts = TEXTS['es' if language in ('spaes', 'spaar') else 'en']
    entries, matched = [], 0
    for entry in bundle.entries:
        if entry.id == 30:
            matched += 1
            messages = fmg.read(entry.data)
            if set(messages).intersection(texts):
                raise ValueError('Altar message IDs already exist; source mod cannot be merged implicitly')
            messages.update(texts)
            entry = replace(entry, data=fmg.write(messages))
        entries.append(entry)
    if matched != 1:
        raise ValueError('Message archive must have exactly one event-text group (30)')
    rewritten = bnd4.write(replace(bundle, entries=tuple(entries)))
    return pack_layout(packed, rewritten, max_size=bnd4.MAX_ARCHIVE)


def rewrite_talk(packed, tool, temporary):
    bundle = bnd4.read(unpack_layout(packed, max_size=bnd4.MAX_ARCHIVE))
    matches = [i for i,e in enumerate(bundle.entries) if e.filename(bundle.unicode) == SCRIPT_NAME]
    if len(matches) != 1:
        raise ValueError('Altar archive must contain exactly one t242307.esd')
    index = matches[0]
    if sha(bundle.entries[index].data) != SCRIPT_INPUT:
        raise ValueError('Altar source is not the supported 1.09 script')
    source, dest = Path(temporary)/'source.esd', Path(temporary)/'rewritten.esd'
    source.write_bytes(bundle.entries[index].data)
    result = subprocess.run([str(Path(tool).resolve()), 'rebirth', str(source), str(dest)],
                            capture_output=True, text=True, timeout=30)
    if result.returncode:
        raise ValueError('Altar script helper refused the source: ' + result.stderr.strip())
    rewritten = dest.read_bytes()
    if sha(rewritten) != SCRIPT_OUTPUT:
        raise ValueError('Altar helper did not produce the known rewrite')
    entries = list(bundle.entries)
    entries[index] = replace(entries[index], data=rewritten)
    return pack_layout(packed, bnd4.write(replace(bundle, entries=tuple(entries))), max_size=bnd4.MAX_ARCHIVE)


def build_rebirth_mod(game, cache, tool):
    """Return a complete generated layer, without changing the dump or user mods.

    Publish only after all source bundles validate. Reuse a verified cache;
    preserve a damaged/edited cache and publish a separate repaired generation.
    """
    game, cache = Path(game).resolve(), Path(cache).resolve()
    if cache == game or cache.is_relative_to(game):
        raise ValueError('Generated altar layer must be outside the original dump')
    source_paths = [game/TALK_PATH] + sorted((game/'dvdroot_ps4/msg').glob('*/menu.msgbnd.dcx'))
    if len(source_paths) < 2:
        raise ValueError('No localized menu message archives found')
    sources = {str(p.relative_to(game)): p.read_bytes() for p in source_paths}
    # Generator dependencies belong in the key, as well as every original file.
    implementation = {name:sha(Path(__file__).with_name(name).read_bytes()) for name in
                      ('rebirth_assets.py','bnd4.py','fmg.py','world_assets.py')}
    signature = {'version':1, 'sources':{name:sha(data) for name,data in sources.items()},
                 'implementation':implementation, 'script_output':SCRIPT_OUTPUT}
    key = sha(json.dumps(signature, sort_keys=True).encode())
    cache.mkdir(parents=True, exist_ok=True)
    for candidate in sorted(cache.glob('rebirth-'+key+'-*')):
        try:
            manifest = json.loads((candidate/'manifest.json').read_text())
            outputs = manifest['outputs']
            if manifest['signature'] == signature and set(outputs) == set(sources) and all(
                    sha((candidate/name).read_bytes()) == digest for name,digest in outputs.items()):
                return candidate
        except (OSError, ValueError, KeyError, TypeError):
            continue
    temporary = Path(tempfile.mkdtemp(prefix='.rebirth-'+key+'-', dir=cache))
    try:
        with tempfile.TemporaryDirectory(prefix='.script-', dir=temporary) as work:
            talk = rewrite_talk(sources[str(TALK_PATH)], tool, work)
        outputs = {}
        for name, original in sources.items():
            generated = talk if name == str(TALK_PATH) else rewrite_messages(original, Path(name).parent.name)
            target = temporary/name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(generated)
            outputs[name] = sha(generated)
        (temporary/'manifest.json').write_text(json.dumps({'signature':signature,'outputs':outputs,
            'scope':'Offline altar assets only; runtime reset/heal/menu/undo service must be integrated before gameplay.'},
            ensure_ascii=False, indent=2)+'\n')
        target = temporary.with_name(temporary.name[1:])
        os.rename(temporary, target)
        return target
    finally:
        if temporary.exists(): shutil.rmtree(temporary)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', required=True)
    parser.add_argument('--cache', required=True)
    parser.add_argument('--tool', required=True)
    args = parser.parse_args()
    try:
        print(build_rebirth_mod(args.game, args.cache, args.tool))
    except (OSError, ValueError, subprocess.TimeoutExpired) as error:
        parser.exit(1, str(error)+'\n')
