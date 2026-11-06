// SPDX-License-Identifier: GPL-3.0-or-later
// Main-thread altar handshake, adapted from BBHOST engine/rebirth.cpp at
// bdc5cdf8be5f37188da9c2368e92be444fdc04cb; explicit backend for lifecycle tests.
#pragma once
#include "rebirth_plan.h"
#include "rebirth_script.h"
#include <array>

namespace BbRebirth {
struct Snapshot { Build build; std::uintptr_t record=0; };
// Backend owns validation/guest calls. A successful Reset/Restore includes
// healing and echoes; preflight must complete before applying any changes.
struct Backend {
    virtual ~Backend()=default;
    virtual bool World(std::uint32_t& block)=0;
    virtual std::uintptr_t TakeMenu()=0;
    virtual bool ReadFlags(std::array<bool,3>& values)=0;
    virtual bool Flag(std::uint32_t id,bool value)=0;
    virtual bool Reset(Snapshot& before)=0;
    virtual bool Restore(const Snapshot& before)=0;
    virtual bool Hold(std::uintptr_t menu)=0;
    virtual std::int32_t Count(std::uintptr_t menu)=0;
    virtual void Release(std::uintptr_t menu)=0;
};
class Session {
    bool loading=true,at_altar=false;
    std::optional<Snapshot> snapshot;
    std::uintptr_t menu=0;
    std::uint64_t waiting_since=0;
    void DropMenu(Backend& b) { if (menu) b.Release(menu);menu=0;waiting_since=0; }
    bool StartOver(Backend& b) {
        bool cleared=true;
        for (auto flag : {rebirth::kReqReset,rebirth::kReqUndo,rebirth::kFail,rebirth::kMenu})
            cleared=b.Flag(flag,false) && cleared;
        snapshot.reset();DropMenu(b);return cleared;
    }
public:
    void Tick(Backend& b,std::uint64_t now_ns) {
        const auto made=b.TakeMenu(); // drain unrelated doll menus in other maps
        std::uint32_t block=0;
        if (!b.World(block)) { loading=true;return; }
        const bool here=block==0x18020000;
        if (loading || here!=at_altar) {
            at_altar=here;loading=!StartOver(b);return;
        }
        if (!here) return;
        std::array<bool,3> flags{};
        if (!b.ReadFlags(flags)) return;
        if (flags[0]) {
            Snapshot before;
            const bool ok=b.Reset(before);
            if (ok) snapshot=before;
            b.Flag(rebirth::kFail,!ok);b.Flag(rebirth::kReqReset,false);
        }
        if (flags[1]) {
            const bool ok=snapshot && b.Restore(*snapshot);
            if (ok) snapshot.reset();
            b.Flag(rebirth::kFail,!ok);b.Flag(rebirth::kReqUndo,false);
        }
        if (flags[2]) {
            if (!menu && made && b.Hold(made)) { menu=made;waiting_since=0; }
            if (menu && b.Count(menu)==1) { DropMenu(b);b.Flag(rebirth::kMenu,false); }
            else if (!menu) {
                if (!waiting_since) waiting_since=now_ns;
                // Two seconds at every FPS, rather than 120 raw frames.
                if (now_ns>=waiting_since && now_ns-waiting_since>=2'000'000'000ull) {
                    waiting_since=0;b.Flag(rebirth::kMenu,false);
                }
            }
        } else DropMenu(b);
    }
};
}
