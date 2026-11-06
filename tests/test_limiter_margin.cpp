// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_limiter.h"
#include <cassert>
#include <cstdio>
#include <limits>
#include <random>
#include <vector>

int main() {
    BbLimiter::Margin margin;
    for (unsigned i=0;i<31;++i) { margin.Observe(0);assert(margin.Microseconds()==2000); }
    margin.Observe(0);assert(margin.Microseconds()==300);
    margin.Observe(5000);
    for (unsigned i=0;i<15;++i) margin.Observe(0);
    assert(margin.Microseconds()==300); // a single exceptional wake does not set the margin
    margin.Observe(500);
    for (unsigned i=0;i<15;++i) margin.Observe(0);
    assert(margin.Microseconds()==700);
    for (unsigned i=0;i<128;++i) margin.Observe(0);
    assert(margin.Microseconds()==300);
    std::mt19937_64 rng(0x4c494d49544552);
    for (int fixed : {-1,0,300,2000,5000}) {
        BbLimiter::Margin clock(fixed);
        std::vector<std::int64_t> history;
        int expected=fixed<0 ? 2000 : fixed;
        for (unsigned i=0;i<10000;++i) {
            const auto late=i%97==0 ? std::numeric_limits<std::int64_t>::max() : std::int64_t(rng()%5000)-100;
            clock.Observe(late);history.push_back(std::max<std::int64_t>(late,0));
            if (history.size()>128) history.erase(history.begin());
            if (fixed<0 && i+1>=32 && (i+1)%16==0) {
                auto ordered=history;std::sort(ordered.begin(),ordered.end());
                const auto second=ordered[ordered.size()-2];
                expected=second>=1800 ? 2000 : int(std::max<std::int64_t>(300,second+200));
            }
            assert(clock.Microseconds()==expected);
        }
    }
    std::puts("Limiter margin: warmup, outliers, window eviction, fixed modes and 50,000 reference samples passed");
}
