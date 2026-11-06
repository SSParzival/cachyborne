"""Retail mirror data, exact known rewrite, DCX boundaries and mod priority."""
from paths import ROOT
import hashlib
import os
from pathlib import Path
import shutil
import struct
import tempfile
import unittest
import zlib

import mods
import world_assets as world


class WorldDcxTests(unittest.TestCase):
    def test_altar_layer_is_opt_in_and_requires_the_helper(self):
        self.assertEqual(world.feature_layers('/absent','/absent'), [])
        for value in (1, 'false', None):
            with self.assertRaises(ValueError):
                world.feature_layers('/absent','/absent',rebirth=value)
        with self.assertRaisesRegex(ValueError,'helper'):
            world.feature_layers('/absent','/absent',rebirth=True)

    def test_disabled_feature_does_not_read_or_generate_assets(self):
        self.assertEqual(world.feature_layers('/nonexistent/game', '/nonexistent/out'), [])
        for invalid in ('0', '1', 0, None):
            with self.assertRaises(ValueError):
                world.feature_layers('/nonexistent/game', '/nonexistent/out', change_appearance=invalid)

    def fixture(self, raw=b'layout'):
        header=bytearray(0x4c)
        header[:4]=b'DCX\0'; header[0x18:0x1c]=b'DCS\0'
        header[0x24:0x2c]=b'DCP\0DFLT'; header[0x44:0x48]=b'DCA\0'
        compressed=zlib.compress(raw)
        struct.pack_into('>II',header,0x1c,len(raw),len(compressed))
        return bytes(header)+compressed

    def test_dcx_round_trip_and_bad_stream_boundaries(self):
        fixture=self.fixture()
        self.assertEqual(world.unpack_layout(fixture),b'layout')
        self.assertEqual(world.unpack_layout(world.pack_layout(fixture,b'new layout')),b'new layout')
        variants=[fixture[:-1],fixture+b'x',fixture[:-4]+b'xxxx']
        for size in (0,1,world.MAX_LAYOUT+1):
            bad=bytearray(fixture);struct.pack_into('>I',bad,0x1c,size);variants.append(bad)
        # A second complete stream must not hide behind a correct first stream.
        bad=bytearray(fixture+zlib.compress(b'extra'))
        struct.pack_into('>I',bad,0x20,len(bad)-0x4c);variants.append(bad)
        for bad in variants:
            with self.subTest(size=len(bad)),self.assertRaises(ValueError): world.unpack_layout(bad)

    def test_unknown_layout_cannot_be_changed(self):
        source=bytearray(b'not the retail map')
        before=bytes(source)
        with self.assertRaises(ValueError): world.mirror_layout(source)
        self.assertEqual(source,before)


class RetailMirrorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.game=Path(os.environ.get('CACHYBORNE_TEST_GAME',ROOT/'CUSA03173'))
        cls.source=cls.game/world.MIRROR_PATH
        if not cls.source.is_file(): raise unittest.SkipTest('retail Dream layout unavailable')

    def test_known_output_and_original_mirror_identity(self):
        original=self.source.read_bytes()
        raw=world.unpack_layout(original)
        changed=world.mirror_layout(raw)
        self.assertEqual(hashlib.sha256(changed).hexdigest(),world.MIRROR_OUTPUT)
        self.assertEqual(len(changed),len(raw))
        _,parts=world._lists(changed)[3]
        mirror=parts[742]
        kind,index=struct.unpack_from('<ii',changed,mirror+0x14)
        entity=mirror+struct.unpack_from('<q',changed,mirror+0xb0)[0]
        typedata=mirror+struct.unpack_from('<q',changed,mirror+0xb8)[0]
        self.assertEqual((kind,index,struct.unpack_from('<i',changed,entity)[0],
                          struct.unpack_from('<i',changed,typedata+0x10)[0]),(2,6,2100202,210696))
        self.assertEqual(world.unpack_layout(original),raw)
        self.assertEqual(self.source.read_bytes(),original)
        with self.assertRaises(ValueError): world.mirror_layout(changed)

    def test_generated_mod_cache_and_player_precedence(self):
        before=hashlib.sha256(self.source.read_bytes()).digest()
        with tempfile.TemporaryDirectory() as folder:
            out=Path(folder)
            layers=world.feature_layers(self.game,out,change_appearance=True)
            self.assertEqual(len(layers),1)
            layer=layers[0][1]
            generated=layer/world.MIRROR_PATH
            stamp=generated.stat().st_mtime_ns
            self.assertEqual(world.build_mirror_mod(self.game,out),layer)
            self.assertEqual(generated.stat().st_mtime_ns,stamp)
            generated.write_bytes(b'corrupted generated cache')
            self.assertEqual(world.build_mirror_mod(self.game,out),layer)
            self.assertEqual(hashlib.sha256(world.unpack_layout(generated.read_bytes())).hexdigest(),world.MIRROR_OUTPUT)
            # Use a minimal source tree rather than linking the full game.
            game=out/'game'; path=game/world.MIRROR_PATH
            path.parent.mkdir(parents=True);shutil.copyfile(self.source,path)
            player=out/'player'; replacement=player/world.MIRROR_PATH
            replacement.parent.mkdir(parents=True);replacement.write_bytes(b'player map')
            overlay=mods.build_overlay(game,out,layers+[('player',player)])
            self.assertEqual((overlay/world.MIRROR_PATH).read_bytes(),b'player map')
            self.assertEqual(path.read_bytes(),self.source.read_bytes())
            overlay=mods.build_overlay(game,out,[('generated mirror',layer)])
            self.assertEqual((overlay/world.MIRROR_PATH).read_bytes(),generated.read_bytes())
        self.assertEqual(hashlib.sha256(self.source.read_bytes()).digest(),before)
