// SPDX-License-Identifier: GPL-3.0-or-later
// Adapted from droogie/bbhost bdc5cdf8be5f37188da9c2368e92be444fdc04cb.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// Hex SHA-256 of a buffer.
std::string sha256_hex(const std::uint8_t* data, std::size_t len);
