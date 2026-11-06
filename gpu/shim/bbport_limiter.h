// SPDX-License-Identifier: GPL-3.0-or-later
// BBHOST CPU-04 limiter tuning, engine/frame_rate.cpp @ bdc5cdf8.
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>

namespace BbLimiter {
// Main-thread-only sleep history. The second-largest of the last 128 wake
// delays plus 200 us gives a 300..2000 us spin margin after 32 samples.
class Margin {
public:
    explicit Margin(int fixed_us=-1) : fixed{fixed_us}, margin{fixed_us<0 ? 2000 : fixed_us} {}
    void Observe(std::int64_t late_us) {
        late[samples%late.size()]=std::max<std::int64_t>(late_us,0);
        ++samples;
        if (fixed>=0 || samples<32 || samples%16) return;
        std::int64_t largest=0,second=0;
        for (std::uint64_t i=0;i<std::min<std::uint64_t>(samples,late.size());++i) {
            const auto value=late[i];
            if (value>largest) { second=largest;largest=value; }
            else if (value>second) second=value;
        }
        // Clamp before adding so even a saturated diagnostic sample is safe.
        margin=static_cast<int>(std::max<std::int64_t>(300,std::min<std::int64_t>(second,1800)+200));
    }
    int Microseconds() const { return margin; }
    std::uint64_t Samples() const { return samples; }
private:
    std::array<std::int64_t,128> late{};
    std::uint64_t samples=0;
    int fixed,margin;
};

// Preserve the retail limiter's arithmetic, histories and flags, but use its
// sleeping wait in both foreground/background and adapt the three wait loads.
// All sites/constant bits are checked before writing. fixed_us=-1 means auto.
bool Install(unsigned char* image,std::uint64_t size,int fixed_us=-1);
}
