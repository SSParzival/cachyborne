// SPDX-License-Identifier: GPL-2.0-or-later
#include "bloodborne_menu_hover.h"
#include <cassert>

int main() {
    BbMenuPointer::WheelHover wheel;
    assert(!wheel.Holds(100, 100));
    wheel.Scroll(100, 100);
    for (int i = 0; i < 1000; ++i) {
        assert(wheel.Holds(100, 100));
        assert(wheel.Holds(102, 98));
    }
    assert(wheel.Holds(116, 100));
    assert(!wheel.Holds(112, 112)); // radial slop, not a square
    assert(!wheel.Holds(100, 100)); // returning does not reacquire the hold
    wheel.Scroll(200, 300);
    assert(wheel.Holds(200, 300));
    assert(!wheel.Holds(200, 317));
}
