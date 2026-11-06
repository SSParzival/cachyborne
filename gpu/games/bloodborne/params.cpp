// SPDX-License-Identifier: GPL-3.0-or-later
#include "params.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <span>

namespace BbParams {
namespace {
bool Read(Reader reader, Address base, std::size_t offset, void* value, std::size_t size) {
    constexpr auto max = std::numeric_limits<Address>::max();
    if (!reader.read || base < 0x10000 || offset > max - base || size > max - base - offset)
        return false;
    return reader.read(reader.context, base + offset, value, size);
}
template<class T> bool Get(Reader reader, Address base, std::size_t offset, T& value) {
    return Read(reader, base, offset, &value, sizeof(value));
}
std::string Name(Reader reader, Address cap) {
    Address length = 0, capacity = 0, at = cap;
    if (!Get(reader,cap,0x28,length) || !Get(reader,cap,0x30,capacity) ||
        !length || length > 64 || capacity < length) return {};
    std::size_t offset = 0x18;
    if (capacity >= 8) {
        if (!Get(reader,cap,0x18,at)) return {};
        offset = 0;
    }
    std::array<char16_t,65> name{};
    if (!Read(reader,at,offset,name.data(),(length+1)*2) || name[length]) return {};
    std::string result;
    for (Address i=0;i<length;++i) {
        if (!name[i] || name[i] >= 128) return {};
        result.push_back(static_cast<char>(name[i]));
    }
    return result;
}
struct Record { std::uint32_t id; Address address; };
std::optional<Record> DecodeRecord(const Table& table,std::span<const unsigned char> bytes) {
    if (table.format > 5) return {};
    const bool wide = table.format > 3 && (table.flags & 2);
    const std::size_t header = table.format == 2 ? 0x34 : 0x40;
    const std::size_t stride = wide ? 0x18 : 0xc;
    if (bytes.size()!=stride) return {};
    std::uint32_t id = 0;
    Address offset = 0;
    std::memcpy(&id,bytes.data(),sizeof(id));
    if (wide) {
        std::memcpy(&offset,bytes.data()+8,sizeof(offset));
    } else {
        std::uint32_t narrow = 0;
        std::memcpy(&narrow,bytes.data()+4,sizeof(narrow));
        offset = narrow;
    }
    if (offset < header + stride * table.rows || offset >= 64u<<20 ||
        table.file > std::numeric_limits<Address>::max()-offset) return {};
    return Record{id,table.file+offset};
}
std::optional<Record> RecordAt(Reader reader, const Table& table, unsigned index) {
    if (index >= table.rows || table.format > 5) return {};
    const std::size_t stride = table.format > 3 && (table.flags & 2) ? 0x18 : 0xc;
    const std::size_t header = table.format == 2 ? 0x34 : 0x40;
    std::array<unsigned char,0x18> record{};
    if (!Read(reader,table.file,header+stride*index,record.data(),stride)) return {};
    return DecodeRecord(table,std::span(record).first(stride));
}
// Keep metadata reads bounded for both lookups and enumeration. A visitor can
// stop after a match without reading or validating unrelated later records.
template<class Visitor>
bool VisitRecords(Reader reader, const Table& table, unsigned limit, Visitor visit) {
    if (table.format > 5 || limit > table.rows) return false;
    constexpr unsigned Batch=128;
    std::array<unsigned char,Batch*0x18> records;
    const std::size_t stride = table.format > 3 && (table.flags & 2) ? 0x18 : 0xc;
    const std::size_t header = table.format == 2 ? 0x34 : 0x40;
    for (unsigned i=0;i<limit;i+=Batch) {
        const unsigned count=std::min<unsigned>(Batch,limit-i);
        if (!Read(reader,table.file,header+stride*i,records.data(),stride*count)) return false;
        for (unsigned j=0;j<count;++j) {
            const auto row=DecodeRecord(table,std::span(records).subspan(j*stride,stride));
            if (!row) return false;
            if (!visit(*row,i+j)) return true;
        }
    }
    return true;
}
}

std::optional<Table> FindTable(Reader reader, Address repository_slot, std::string_view name) {
    if (name.empty() || name.size() > 64) return {};
    Address repo = 0;
    if (!Get(reader,repository_slot,0,repo)) return {};
    for (unsigned slot=0;slot<63;++slot) {
        const std::size_t at = 0x70 + slot*0x48;
        std::int32_t count = 0;
        if (!Get(reader,repo,at,count) || count < 1 || count > 8) continue;
        for (int i=0;i<count;++i) {
            Address cap=0,holder=0,file=0;
            if (!Get(reader,repo,at+8+8*i,cap) || Name(reader,cap)!=name ||
                !Get(reader,cap,0x70,holder) || !Get(reader,holder,0x70,file)) continue;
            Table result{.file=file,.rows=0,.format=0,.flags=0,.type={}};
            std::array<char,32> type{};
            if (!Get(reader,file,0xa,result.rows) || !Get(reader,file,0x2d,result.format) ||
                !Get(reader,file,0x2e,result.flags) || !Read(reader,file,0xc,type.data(),type.size()) ||
                !result.rows || result.format > 5 || !type.front() || !std::memchr(type.data(),0,type.size())) return {};
            result.type = type.data();
            return result;
        }
    }
    return {};
}

std::optional<Row> FindRow(Reader reader, const Table& table, std::uint32_t id) {
    if (table.format > 5) return {};
    std::size_t bytes=0;
    const auto first=RecordAt(reader,table,0), second=RecordAt(reader,table,1);
    if (first && second && second->address > first->address && second->address-first->address <= 1u<<20)
        bytes=second->address-first->address;
    // Retain first-match order for repeated or unsorted IDs. Read bounded
    // batches instead of making two process_vm_readv calls for every record;
    // no borrowed pointer survives this operation and no heap cache is needed.
    std::optional<Row> result;
    if (!VisitRecords(reader,table,table.rows,[&](const Record& row,unsigned) {
        if (row.id!=id) return true;
        result=Row{row.address,bytes};
        return false;
    })) return {};
    return result;
}

bool ListIds(Reader reader, const Table& table, std::uint32_t* ids,
             std::size_t capacity, std::size_t* count) {
    if (table.format > 5) return false;
    if (count) *count=table.rows;
    if (!ids) return true;
    const auto limit=static_cast<unsigned>(std::min<std::size_t>(capacity,table.rows));
    return VisitRecords(reader,table,limit,[&](const Record& row,unsigned index) {
        ids[index]=row.id;
        return true;
    });
}

std::optional<std::uint32_t> ReadWord(Reader reader, const Row& row, std::size_t offset) {
    if (offset > row.bytes || row.bytes-offset < 4) return {};
    std::uint32_t word=0;
    if (!Get(reader,row.address,offset,word)) return {};
    return word;
}
}
