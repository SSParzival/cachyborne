"""Spanish in-game UI must have a real catalog, independent of voice selection."""
from paths import ROOT
import json
import re
import unittest


class SpanishMenuTests(unittest.TestCase):
    def test_overlay_literal_strings_have_spanish_translations(self):
        header = (ROOT / 'gpu/shim/bbport_spanish.h').read_text()
        entries = re.findall(r'\{("(?:[^"\\]|\\.)*"), ("(?:[^"\\]|\\.)*")\}', header)
        catalog = {json.loads(en): json.loads(es) for en, es in entries}
        source = (ROOT / 'gpu/shim/bbport_overlay.cpp').read_text()
        literals = re.findall(r'BbSettings::MenuText\(\s*((?:"(?:[^"\\]|\\.)*"\s*)+)', source)
        for literal in literals:
            text = ''.join(json.loads(t) for t in re.findall(r'"(?:[^"\\]|\\.)*"', literal))
            with self.subTest(text=text):
                self.assertIn(text, catalog)
        placeholders = r'%(?:\.[0-9]+)?[0-9]*[dsfux]'
        for english, spanish in catalog.items():
            with self.subTest(english=english):
                self.assertEqual(re.findall(placeholders, english), re.findall(placeholders, spanish))
        self.assertEqual(catalog['Display'], 'Pantalla')
        self.assertEqual(catalog['Language'], 'Idioma')
