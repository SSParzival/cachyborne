// SPDX-License-Identifier: GPL-2.0-or-later
// Exercise the actual UTF-16 text supplied to the game's Scaleform menu.
#include "../gpu/shim/bbport_game_menu.cpp"
#include <cassert>

int main() {
    using namespace BbGameMenu;
    auto& settings = BbSettings::Get();
    settings.menu_language = BbSettings::MenuLanguage::Spanish;
    setenv("BB_VOICE_LANGUAGE", "eng", 1);
    DefineTexts();
    assert(pages.size() == 2);
    assert(std::u16string(TextOf(pages[0].name)) == u"Pantalla");
    assert(std::u16string(TextOf(pages[1].name)) == u"Efectos");
    assert(std::u16string(TextOf(pages[0].rows[0].name)) == u"Relación de aspecto");
    assert(std::u16string(TextOf(pages[1].rows[0].name)) == u"Aberración cromática");
    RefreshOutputs(BbSettings::Aspect16x9);
    assert(std::u16string(TextOf(AutoText)).starts_with(u"Automático ("));
    // Speech preferences must never switch these labels to English/Japanese.
    setenv("BB_VOICE_LANGUAGE", "jaj", 1);
    assert(std::u16string(TextOf(pages[0].name)) == u"Pantalla");
    settings.menu_language = BbSettings::MenuLanguage::English;
    assert(std::u16string(TextOf(pages[0].name)) == u"Display");
    puts("PASS: Spanish native game-menu labels, accents and voice independence");
}
