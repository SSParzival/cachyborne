// SPDX-License-Identifier: GPL-2.0-or-later
#include "games/bloodborne/rebirth_session.h"
#include <cassert>
#include <map>
#include <iostream>
using namespace BbRebirth;
using namespace rebirth;
struct Fake final : Backend {
    std::uint32_t block=0x18020000;
    bool world=true,ready=true,reset_ok=true,restore_ok=true;
    unsigned reads=0,resets=0,restores=0,holds=0,releases=0;
    std::uintptr_t made=0;
    std::map<std::uint32_t,bool> flags;
    std::map<std::uintptr_t,int> refs;
    Build build{30,{20,20,15,15,15,15},12345};
    bool World(std::uint32_t& out) override { out=block;return world; }
    std::uintptr_t TakeMenu() override { auto value=made;made=0;return value; }
    bool ReadFlags(std::array<bool,3>& out) override {
        ++reads;if (!ready) return false;
        out={flags[kReqReset],flags[kReqUndo],flags[kMenu]};return true;
    }
    bool Flag(std::uint32_t id,bool value) override { if (!ready) return false;flags[id]=value;return true; }
    bool Reset(Snapshot& before) override { ++resets;if (!reset_ok) return false;before={build,1};build.level=10;build.echoes+=999;return true; }
    bool Restore(const Snapshot& before) override { ++restores;if (!restore_ok) return false;build=before.build;return true; }
    bool Hold(std::uintptr_t menu) override { if (refs[menu]<1) return false;++holds;++refs[menu];return true; }
    std::int32_t Count(std::uintptr_t menu) override { return refs[menu]; }
    void Release(std::uintptr_t menu) override { assert(refs[menu]>0);++releases;--refs[menu]; }
};
int main() {
    Session s;Fake b;s.Tick(b,1);
    const auto before=b.build;
    b.flags[kReqReset]=true;s.Tick(b,2);
    assert(b.resets==1 && b.build.level==10 && !b.flags[kReqReset] && !b.flags[kFail]);
    b.flags[kReqUndo]=true;s.Tick(b,3);
    assert(b.restores==1 && b.build==before && !b.flags[kReqUndo] && !b.flags[kFail]);
    b.flags[kReqUndo]=true;s.Tick(b,4);assert(b.restores==1 && b.flags[kFail]);
    b.reset_ok=false;b.flags[kReqReset]=true;s.Tick(b,5);assert(b.build==before && b.flags[kFail]);
    b.reset_ok=true;b.flags[kReqReset]=true;s.Tick(b,6);assert(!b.flags[kFail]);
    b.made=123;b.refs[123]=1;b.flags[kMenu]=true;s.Tick(b,7);
    assert(b.holds==1 && b.refs[123]==2 && b.flags[kMenu]);
    --b.refs[123];s.Tick(b,8);
    assert(b.releases==1 && b.refs[123]==0 && !b.flags[kMenu]);
    // Loading invalidates undo when the next world arrives, including a new
    // character at the same map. Held menu references release exactly once.
    b.made=456;b.refs[456]=1;b.flags[kMenu]=true;s.Tick(b,9);
    b.world=false;s.Tick(b,10);assert(b.refs[456]==2);
    b.world=true;s.Tick(b,11);assert(b.refs[456]==1 && b.releases==2);
    b.flags[kReqUndo]=true;s.Tick(b,12);assert(b.restores==1 && b.flags[kFail]);
    b.block=0x1a000000;s.Tick(b,13);const auto reads=b.reads;
    for (unsigned i=0;i<10000;++i) { b.made=789;s.Tick(b,14+i);assert(b.made==0); }
    assert(b.reads==reads && b.holds==2); // no event flag polling outside altar
    // Loading before flag service readiness must not accept stale save flags.
    b.block=0x18020000;b.ready=false;b.flags[kReqReset]=true;s.Tick(b,20000);
    b.ready=true;s.Tick(b,20001);assert(!b.flags[kReqReset]);
    // Menu timeout is two real seconds at every frame rate, including a menu
    // which never appears or an already-dead constructor observation.
    for (unsigned fps : {30,60,144,480}) {
        Session timer;Fake f;timer.Tick(f,1);f.flags[kMenu]=true;f.made=999;f.refs[999]=0;
        timer.Tick(f,1'000'000'000);
        for (unsigned frame=1;frame<2*fps;++frame) {
            timer.Tick(f,1'000'000'000+std::uint64_t(frame)*1'000'000'000/fps);
            assert(f.flags[kMenu]);
        }
        timer.Tick(f,3'000'000'000);assert(!f.flags[kMenu] && f.holds==0);
    }
    std::cout << "ok: altar reset/undo, failure, loading, menu ownership/timeout, no flag polling elsewhere\n";
}
