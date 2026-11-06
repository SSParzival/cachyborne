// SPDX-License-Identifier: GPL-3.0-or-later
#include "player_data.h"
#include <array>
#include <cstring>
namespace BbPlayerData {
namespace {
bool ReadAt(BbParams::Reader r,std::uintptr_t at,void* out,std::size_t size) {
    return r.read && at>=0x10000 && size<=UINTPTR_MAX-at && r.read(r.context,at,out,size);
}
struct Field { std::string_view name;std::uint32_t offset;bool manager,signed_value; };
constexpr Field Fields[]{
    {"hp",0x14,false,true},{"max_hp",0x18,false,true},{"stamina",0x30,false,true},{"max_stamina",0x34,false,true},
    {"vitality",0x40,false,true},{"endurance",0x48,false,true},{"strength",0x58,false,true},{"skill",0x60,false,true},
    {"bloodtinge",0x68,false,true},{"arcane",0x70,false,true},{"insight",0x84,false,true},{"level",0x90,false,true},
    {"echoes",0x94,false,true},{"ng_cycle",0x68,true,true},{"coop_helps",0x78,true,false},
    {"invader_kills",0x7c,true,false},{"true_deaths",0x84,true,false},{"deaths",0x88,true,false},{"play_time_ms",0x94,true,false}
};
}
std::optional<Player> Read(BbParams::Reader reader,std::uintptr_t manager_slot) {
    Player p;
    std::array<std::uint8_t,0xd0> record{};
    if (!ReadAt(reader,manager_slot,&p.manager,8) || p.manager>UINTPTR_MAX-8 ||
        !ReadAt(reader,p.manager+8,&p.record,8) || !ReadAt(reader,p.record,record.data(),record.size())) return {};
    auto word=[&](std::size_t at) { std::int32_t v;std::memcpy(&v,record.data()+at,4);return v; };
    p.build.level=word(0x90);p.build.echoes=word(0x94);p.origin=record[0xce];
    constexpr std::size_t offsets[]{0x40,0x48,0x58,0x60,0x68,0x70};
    for (unsigned i=0;i<6;++i) p.build.stats[i]=word(offsets[i]);
    return p;
}
std::optional<std::int64_t> Stat(BbParams::Reader reader,const Player& p,std::string_view name) {
    for (const auto& field : Fields) if (field.name==name) {
        const auto base=field.manager ? p.manager : p.record;
        std::uint32_t v=0;
        if (!p.record || base>UINTPTR_MAX-field.offset || !ReadAt(reader,base+field.offset,&v,4)) return {};
        return field.signed_value ? static_cast<std::int64_t>(static_cast<std::int32_t>(v)) : static_cast<std::int64_t>(v);
    }
    return {};
}
}
