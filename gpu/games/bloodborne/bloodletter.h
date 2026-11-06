// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "games/bloodborne/params.h"
#include <cstdint>

namespace BbBloodletter {
using Writer = bool (*)(void*, BbParams::Address, const void*, std::size_t);
enum class Result { Unavailable, Custom, AlreadyCorrected, Corrected, WriteRefused };
// Borrow the repository only for this operation, on the game thread while
// resources are stable. Only the aligned original radius word may be written.
Result Correct(BbParams::Reader reader, Writer writer, BbParams::Address repository_slot);
bool Install(unsigned char* image, std::uint64_t size);
}
