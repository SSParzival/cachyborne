# SPDX-License-Identifier: GPL-3.0-or-later
"""Test-only offline oracle for the native Bloodletter correction; never edit the dump.

Bloodletter correction adapted from BBHOST engine/frame_rate.cpp at
bdc5cdf8be5f37188da9c2368e92be444fdc04cb, GPL-3.0-or-later. This fixture
is not part of setup or normal launch; native code corrects loaded rows.
"""
import hashlib
import json
from pathlib import Path
import shutil
import struct
import tempfile

from param_tables import (apply_overrides, archive, build_mod, load_table_data,
                          table_rows)

PARAM_PATH = Path('dvdroot_ps4/param/gameparam/gameparam.parambnd.dcx')
TABLE = 'AtkParam_Pc'
ROW = 2900931
FIELD = 'hit0_Radius'
OLD_BITS, NEW_BITS = 0x3ecccccd, 0x3f19999a


def bloodletter_patch(raw, layout):
    """Change only the original radius; an existing player change wins."""
    field = next((f for f in layout.fields if f.name == FIELD), None)
    if field is None or (field.type, field.size, field.count, field.bits) != ('f32', 4, 1, 0):
        raise ValueError('Bloodletter correction requires a scalar f32 hit0_Radius')
    rows = [row for key, row in table_rows(raw, layout) if key == ROW]
    if len(rows) > 1:
        raise ValueError('Ambiguous Bloodletter self-hit row')
    if not rows or struct.unpack('<I', struct.pack('<f', rows[0][FIELD]))[0] != OLD_BITS:
        return None
    edits = {'row': {str(ROW): {FIELD: struct.unpack('<f', struct.pack('<I', NEW_BITS))[0]}}}
    result = apply_overrides(raw, layout, edits)
    changed = [i for i, (a, b) in enumerate(zip(raw, result)) if a != b]
    if (len(result) != len(raw) or len(changed) != 4 or
            changed != list(range(changed[0], changed[0] + 4)) or
            raw[changed[0]:changed[0]+4] != struct.pack('<I', OLD_BITS) or
            result[changed[0]:changed[0]+4] != struct.pack('<I', NEW_BITS)):
        raise ValueError('Bloodletter correction changed more than its radius word')
    return result, edits


def build_bloodletter_mod(game, out):
    """Return a complete new layer, or None for a non-original/missing radius.

    Player layers belong after this one. Publication never replaces an old
    generation, and a failed build removes only its own temporary directory.
    """
    game, out = Path(game).resolve(), Path(out).resolve()
    if out.is_relative_to(game):
        raise ValueError('Timing assets must be outside the game dump')
    raw, layout = load_table_data(game, TABLE)
    patch = bloodletter_patch(raw, layout)
    if patch is None:
        return None
    corrected, edits = patch
    source_hash = hashlib.sha256((game/PARAM_PATH).read_bytes()).hexdigest()
    out.mkdir(parents=True, exist_ok=True)
    layer = Path(tempfile.mkdtemp(prefix='bloodletter-', dir=out))
    try:
        target = build_mod(game, TABLE, edits, layer)
        generated = archive(target)
        if generated[TABLE+'.param'] != corrected:
            raise ValueError('Generated Bloodletter table differs from verified correction')
        if hashlib.sha256((game/PARAM_PATH).read_bytes()).hexdigest() != source_hash:
            raise ValueError('Source parameters changed while preparing the correction')
        manifest = {'feature': 'bloodletter-self-hit-above-30', 'table': TABLE, 'row': ROW,
                    'field': FIELD, 'from_bits': OLD_BITS, 'to_bits': NEW_BITS,
                    'source_sha256': source_hash,
                    'output_sha256': hashlib.sha256(target.read_bytes()).hexdigest(),
                    'scope': 'One prepared radius word; player mod layers have priority.'}
        (layer/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
        return layer
    except BaseException:
        shutil.rmtree(layer)
        raise
