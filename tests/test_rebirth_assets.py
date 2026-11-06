# SPDX-License-Identifier: GPL-2.0-or-later
from paths import ROOT
from dataclasses import replace
import hashlib
import json
from pathlib import Path
import tempfile
import unittest

import bnd4
import fmg
import rebirth_assets as altar
from world_assets import pack_layout, unpack_layout


class RebirthAssets(unittest.TestCase):
    def setUp(self):
        self.game=ROOT/'CUSA03173'
        self.tool=ROOT/'out/gpu/rebirth-script-tool'
        if not (self.game/altar.TALK_PATH).is_file() or not self.tool.is_file():
            self.skipTest('Local 1.09 dump and built rebirth helper required')

    def test_full_local_layer_preserves_originals_and_adds_only_altar_content(self):
        with tempfile.TemporaryDirectory() as temp:
            layer=altar.build_rebirth_mod(self.game,temp,self.tool)
            manifest=json.loads((layer/'manifest.json').read_text())
            for name,digest in manifest['signature']['sources'].items():
                original=(self.game/name).read_bytes()
                self.assertEqual(hashlib.sha256(original).hexdigest(),digest)
                before=bnd4.read(unpack_layout(original,max_size=bnd4.MAX_ARCHIVE))
                after=bnd4.read(unpack_layout((layer/name).read_bytes(),max_size=bnd4.MAX_ARCHIVE))
                self.assertEqual([(e.name,e.id,e.flags) for e in after.entries],
                                 [(e.name,e.id,e.flags) for e in before.entries])
                for old,new in zip(before.entries,after.entries):
                    if name==str(altar.TALK_PATH) and old.filename(before.unicode)==altar.SCRIPT_NAME:
                        self.assertEqual(hashlib.sha256(new.data).hexdigest(),altar.SCRIPT_OUTPUT)
                    elif name!=str(altar.TALK_PATH) and old.id==30:
                        texts=fmg.read(new.data)
                        old_texts=fmg.read(old.data)
                        language='es' if Path(name).parent.name in ('spaes','spaar') else 'en'
                        self.assertEqual({k:v for k,v in texts.items() if k not in altar.TEXTS[language]},old_texts)
                        self.assertEqual({k:texts[k] for k in altar.TEXTS[language]},altar.TEXTS[language])
                    else:
                        self.assertEqual(new.data,old.data)
            self.assertEqual(altar.build_rebirth_mod(self.game,temp,self.tool),layer)
            # Cache damage is preserved for inspection; a complete replacement
            # generation is published separately and then reused.
            altered=layer/altar.TALK_PATH
            altered.write_bytes(b'edited cache')
            repaired=altar.build_rebirth_mod(self.game,temp,self.tool)
            self.assertNotEqual(repaired,layer)
            self.assertEqual(altered.read_bytes(),b'edited cache')
            self.assertEqual(altar.build_rebirth_mod(self.game,temp,self.tool),repaired)

    def test_invalid_late_message_source_does_not_publish_partial_layer(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);game=root/'game';cache=root/'cache'
            path=game/altar.TALK_PATH;path.parent.mkdir(parents=True);path.write_bytes((self.game/altar.TALK_PATH).read_bytes())
            source=self.game/'dvdroot_ps4/msg/spaes/menu.msgbnd.dcx'
            for language in ('spaes','zz-invalid'):
                dest=game/'dvdroot_ps4/msg'/language/'menu.msgbnd.dcx'
                dest.parent.mkdir(parents=True)
                dest.write_bytes(source.read_bytes() if language=='spaes' else b'bad last source')
            with self.assertRaises(ValueError): altar.build_rebirth_mod(game,cache,self.tool)
            self.assertEqual(list(cache.iterdir()),[])
            self.assertEqual(path.read_bytes(),(self.game/altar.TALK_PATH).read_bytes())

    def test_collision_wrong_script_and_source_output_alias_refused(self):
        source=(self.game/'dvdroot_ps4/msg/spaes/menu.msgbnd.dcx').read_bytes()
        bundle=bnd4.read(unpack_layout(source,max_size=bnd4.MAX_ARCHIVE))
        entries=[]
        for entry in bundle.entries:
            if entry.id==30:
                messages=fmg.read(entry.data);messages[14000332]='user mod'
                entry=replace(entry,data=fmg.write(messages))
            entries.append(entry)
        collision=pack_layout(source,bnd4.write(replace(bundle,entries=tuple(entries))),max_size=bnd4.MAX_ARCHIVE)
        with self.assertRaisesRegex(ValueError,'already exist'): altar.rewrite_messages(collision,'spaes')
        with self.assertRaisesRegex(ValueError,'outside'): altar.build_rebirth_mod(self.game,self.game/'generated',self.tool)
        with tempfile.TemporaryDirectory() as temp:
            # A wrong helper must not supply an unchecked output.
            with self.assertRaisesRegex(ValueError,'known rewrite'):
                script=Path(temp)/'wrong-helper'
                script.write_text('#!/bin/sh\nprintf bad > "$3"\n');script.chmod(0o700)
                altar.rewrite_talk((self.game/altar.TALK_PATH).read_bytes(),script,temp)
