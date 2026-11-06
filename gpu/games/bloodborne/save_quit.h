// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
namespace BbSaveQuit {
using Confirm = void (*)();
bool Install(unsigned char* image,std::uint64_t size,Confirm confirmed = nullptr);
bool Available();
// Synchronous question construction only. The affirmative callback can run later
// on another thread; the marker follows the game's cloned functor, not TLS.
bool Asking();
void* OpenQuestion(void* out,void* arg);
}
