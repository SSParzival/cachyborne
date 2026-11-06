// SPDX-License-Identifier: GPL-3.0-or-later
// BBHOST v0.2.24 engine/menu_steps: one constructor observer for menu features.
#pragma once
#include <cstdint>

namespace BbMenuSteps {
// Register at startup, before the watched menu can be constructed. The name
// must remain alive. Returns -1 on unsupported code, duplicate watch or capacity.
int Watch(unsigned char* image,std::uint64_t size,std::uint64_t type,const char16_t* name);
// Borrowed constructor result; consume on the main thread after construction.
// Caller must establish that the menu is still alive before holding a reference.
std::uintptr_t Take(int watch);
std::int32_t Count(std::uintptr_t step);
bool Hold(std::uintptr_t step);
// On the main thread, releases one owned reference and destroys through the
// guest vtable when the count reaches zero. Never call twice on a freed step.
bool Release(std::uintptr_t step);
}
