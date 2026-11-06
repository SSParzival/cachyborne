// SPDX-License-Identifier: GPL-2.0-or-later
#include "games/bloodborne/rebirth_plan.h"
#include <cassert>
#include <limits>
#include <iostream>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>
using namespace BbRebirth;
int main() {
    const Origin origin{10,{11,10,12,10,9,8}};
    const CostGraph graph{.1f,1.f,.02f,92.f};
    Build cur{10,origin.stats,123};
    const auto zero = PlanReset(cur,origin,graph);
    assert(zero && zero->after == cur && zero->refund == 0);
    cur.level = 11; ++cur.stats[0];
    const Build untouched = cur;
    const auto one = PlanReset(cur,origin,graph);
    assert(one && one->refund == 829 && one->after.echoes == 952);
    assert(one->before == untouched && cur == untouched && one->bought_points == 1);
    // BBHOST refunds bought attributes even if an edited level disagrees.
    cur.level = 30;
    assert(PlanReset(cur,origin,graph)->refund == 829);
    cur.echoes = EchoCap - 829;
    assert(PlanReset(cur,origin,graph)->after.echoes == EchoCap);
    ++cur.echoes; assert(!PlanReset(cur,origin,graph));
    cur.echoes = -1; assert(!PlanReset(cur,origin,graph));
    cur = untouched; cur.stats[1] = 100; assert(!PlanReset(cur,origin,graph));
    cur = untouched; cur.level = std::numeric_limits<std::int32_t>::max(); assert(!PlanReset(cur,origin,graph));
    for (const float bad : {std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN(),
                            std::numeric_limits<float>::max(), -10000.f}) {
        CostGraph broken = graph; broken[0] = bad;
        assert(!PlanReset(untouched,origin,broken));
    }
    std::int64_t previous = 0;
    for (int level = 1; level <= 544; ++level) {
        const auto cost = LevelCost(level,graph); assert(cost && *cost >= 0);
        assert(*cost >= previous); previous = *cost;
    }
    std::array<std::uint8_t,0xc9> row{};
    std::int16_t level = 10; std::memcpy(row.data()+0xc0,&level,2);
    constexpr std::size_t positions[] = {0xc2,0xc3,0xc5,0xc6,0xc7,0xc8};
    for (std::size_t i=0;i<6;++i) row[positions[i]]=origin.stats[i];
    assert(DecodeOrigin(row)->stats == origin.stats);
    assert(!DecodeOrigin(std::span(row).first(0xc8)));
    std::array<std::uint8_t,0x4c> graphrow{};
    std::memcpy(graphrow.data()+0x3c,graph.data(),sizeof(graph));
    assert(*DecodeGraph(graphrow) == graph);
    assert(!DecodeGraph(std::span(graphrow).first(0x4b)));
    if (const char* folder = std::getenv("CACHYBORNE_REBIRTH_PARAMS")) {
        auto read = [&](const std::string& name) {
            std::ifstream f(std::filesystem::path(folder)/name,std::ios::binary); assert(f);
            return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(f)),{});
        };
        const auto retail_graph = DecodeGraph(read("graph-200.bin")); assert(retail_graph);
        for (int id=3000; id<=3008; ++id) {
            const auto retail_origin = DecodeOrigin(read("origin-"+std::to_string(id)+".bin")); assert(retail_origin);
            Build played{retail_origin->level,retail_origin->stats,123};
            int remaining = 100;
            for (auto& stat : played.stats) {
                const int add = std::min(remaining,99-stat); stat+=add; remaining-=add; played.level+=add;
            }
            assert(remaining==0);
            const auto plan = PlanReset(played,*retail_origin,*retail_graph); assert(plan);
            std::int64_t expected = 0;
            for (int lv=retail_origin->level;lv<played.level;++lv) expected+=*LevelCost(lv,*retail_graph);
            assert(plan->refund==expected && plan->bought_points==100 && plan->after.stats==retail_origin->stats);
        }
        std::cout << "ok: all nine local retail origin rows and actual cost graph\n";
    }
    std::cout << "ok: rebirth planning, all 544 level costs, cap/invalid input, origin/graph layout; no runtime writes\n";
}
