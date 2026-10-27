"""Independent text/voice settings survive persistence and execution-plan export."""
from paths import ROOT
import copy
import tempfile
from pathlib import Path
import unittest
from unittest.mock import patch
import cachyborne as core
import cachyborne_options as options


class VoiceLanguageTests(unittest.TestCase):
    def test_bilingual_defaults_and_explicit_selection_roundtrip(self):
        for voice in dict(options.VOICE_LANGUAGES):
            settings = copy.deepcopy(core.DEFAULTS) | {'voice_language': voice}
            settings = options.reconcile(settings)
            with tempfile.TemporaryDirectory() as directory, patch.object(core, 'CONFIG', Path(directory)):
                core.save_preferences(settings)
                loaded = core.preferences()
            profile = dict(settings=loaded, prepared='/prepared', user='/user',
                           game='/game', probe='/probe', cache='/cache')
            _, env = core.launch_command(profile, base_env={})
            self.assertEqual(env['BB_LANGUAGE'], '20')
            self.assertEqual(env['BB_VOICE_LANGUAGE'], voice)
            self.assertEqual(env['BB_VK_RECORD_THREADS'], '3')
            self.assertEqual(env['BB_ASYNC_SUBMIT'], '1')
            self.assertEqual(env['BB_LAYER_MEMORY'], '1')
            self.assertEqual(env['BB_DMEM_MB'], '5056')
            self.assertEqual(env['MESA_SHADER_CACHE_DIR'], '/cache/mesa')

    def test_invalid_voice_rejected(self):
        with self.assertRaisesRegex(ValueError, 'voces'):
            core.validate_settings(copy.deepcopy(core.DEFAULTS) | {'voice_language': '../eng'})

    def test_stylesheet_accepts_missing_display_and_high_contrast_updates(self):
        import cachyborne_gui as gui
        with patch.object(gui.Gdk.Display, 'get_default', return_value=None), \
             patch.object(gui.Gtk.StyleContext, 'add_provider_for_display') as add, \
             patch.object(gui.Gtk.StyleContext, 'remove_provider_for_display') as remove:
            provider = gui.apply_style()
            gui.Adw.StyleManager.get_default().notify('high-contrast')
            self.assertIsNotNone(provider)
            add.assert_not_called()
            remove.assert_not_called()

    def test_preliminary_validation_explains_missing_dump(self):
        with tempfile.TemporaryDirectory() as directory:
            settings = copy.deepcopy(core.DEFAULTS) | {'game': directory}
            self.assertIn('eboot.bin', core.preparation_status(settings))
