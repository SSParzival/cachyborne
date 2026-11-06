// SPDX-License-Identifier: GPL-3.0-or-later
// Read-only SoloParamRepository access, adapted from BBHOST engine/params.cpp
// at bdc5cdf8be5f37188da9c2368e92be444fdc04cb. No per-frame polling or cache.
#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace BbParams {
using Address = std::uintptr_t;
struct Reader {
    void* context;
    bool (*read)(void* context, Address address, void* destination, std::size_t size);
};
struct Table {
    Address file;
    std::uint16_t rows;
    std::uint8_t format, flags;
    std::string type;
};
struct Row {
    Address address;
    std::size_t bytes; // 0 when the neighboring records cannot establish a stride
};
// The caller runs on the game thread, while table resources are stable. Returned
// addresses are borrowed for that operation; never cache them across loads.
std::optional<Table> FindTable(Reader reader, Address repository_slot, std::string_view name);
std::optional<Row> FindRow(Reader reader, const Table& table, std::uint32_t id);
// BBHOST param_ids semantics: copy at most capacity IDs in file order, keeping
// duplicates; count receives the full row count. A null ids asks only for count.
// On a failed read the output may contain a prefix; callers must reject it.
bool ListIds(Reader reader, const Table& table, std::uint32_t* ids,
             std::size_t capacity, std::size_t* count = nullptr);
std::optional<std::uint32_t> ReadWord(Reader reader, const Row& row, std::size_t offset);
}
