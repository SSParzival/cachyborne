// SPDX-License-Identifier: GPL-3.0-or-later
// Read-only player-record service, adapted from BBHOST engine/player_data.cpp
// at bdc5cdf8be5f37188da9c2368e92be444fdc04cb. No polling or cached guest pointers.
#pragma once
#include "params.h"
#include "rebirth_plan.h"
#include <string_view>
namespace BbPlayerData {
struct Player {
    std::uintptr_t manager=0,record=0;
    BbRebirth::Build build;
    std::uint8_t origin=0;
};
std::optional<Player> Read(BbParams::Reader reader,std::uintptr_t manager_slot);
std::optional<std::int64_t> Stat(BbParams::Reader reader,const Player& player,std::string_view name);
}
