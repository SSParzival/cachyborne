"""Keep current launcher messages and their formatting complete in Spanish."""
from paths import ROOT
import ast
import string
import unittest

import bbport_i18n as i18n
import cachyborne as core
import game_check


class SpanishCatalogTests(unittest.TestCase):
    def test_current_english_catalog_is_complete_in_spanish(self):
        self.assertEqual(set(i18n.EN) - set(i18n.ES), set())

    def test_every_literal_translation_call_has_a_spanish_entry(self):
        for path in (ROOT / 'launcher').glob('*.py'):
            for node in ast.walk(ast.parse(path.read_text())):
                if (isinstance(node, ast.Call) and isinstance(node.func, ast.Name) and
                        node.func.id == 'tr' and node.args and
                        isinstance(node.args[0], ast.Constant) and isinstance(node.args[0].value, str)):
                    with self.subTest(file=path.name, line=node.lineno):
                        self.assertIn(node.args[0].value, i18n.ES)

    def test_format_fields_and_newlines_survive_translation(self):
        formatter = string.Formatter()
        for english in i18n.EN:
            spanish = i18n.ES[english]
            fields = lambda text: [(field, spec, conversion)
                                   for _, field, spec, conversion in formatter.parse(text)
                                   if field is not None]
            with self.subTest(message=english):
                self.assertTrue(spanish.strip())
                self.assertEqual(fields(english), fields(spanish))
                self.assertEqual(english.count('\n'), spanish.count('\n'))

    def test_translator_uses_spanish_and_retains_unknown_diagnostics(self):
        previous = i18n.language()
        try:
            i18n.set_language('es')
            self.assertEqual(i18n.tr('Game text language'), 'Idioma del texto del juego')
            self.assertEqual(i18n.tr('Voice language'), 'Idioma de las voces')
            self.assertEqual(i18n.tr('Could not start: {}').format('EIO'), 'No se pudo iniciar: EIO')
            self.assertEqual(i18n.tr('EIO: custom diagnostic'), 'EIO: custom diagnostic')
        finally:
            i18n.set_language(previous)

    def test_midnight_validation_errors_are_localized(self):
        previous = i18n.language()
        try:
            i18n.set_language('es')
            with self.assertRaisesRegex(ValueError, 'Lista de CPU inválida: p_cpus'):
                core.validate_settings(core.DEFAULTS | {'p_cpus': 'invalid'})
            for kind in ('missing_update', 'wrong_eboot', 'patched_eboot', 'other_title',
                         'unreadable', 'damaged_files'):
                diagnostic = game_check.explain(kind, 'CUSA03173', '01.00', language='es')
                self.assertTrue(diagnostic.startswith('Se encontró CUSA03173, versión 01.00'))
                self.assertIn('1.09', diagnostic)
        finally:
            i18n.set_language(previous)
