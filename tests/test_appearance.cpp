// SPDX-License-Identifier: GPL-2.0-or-later
// Real retail constructor/pacer observers; window/heap calls are spies because
// a standalone test has no renderer. Real mirror interaction is a separate gate.
#include "retail_timing_kit.h"
#include "games/bloodborne/appearance.h"
#include "games/bloodborne/menu_steps.h"
#include <thread>

namespace {
u64 descriptor=0,current_step=0;
unsigned allocations=0,frees=0,closes=0,updates=0,destroyed=0;
bool fail_allocate=false,fail_window=false,drop_game_reference=false;
alignas(16) std::array<u8,0x30> heap{};
alignas(16) std::array<u8,0x10> controller{};
std::array<u64,15> heap_table{};
std::array<u64,4> controller_table{};
std::array<u64,1> step_table{};
u64 __attribute__((sysv_abi)) Allocate(u64 self,u64 size,u64 alignment) {
    assert(self==reinterpret_cast<u64>(heap.data()) && size==0xc0 && alignment==0x10);
    assert(BbMenuSteps::Count(current_step)==2); // held BEFORE any guest allocator callback
    if (fail_allocate) return 0;
    ++allocations;
    auto* memory=std::aligned_alloc(alignment,size);assert(memory);
    return reinterpret_cast<u64>(memory);
}
u64 __attribute__((sysv_abi)) Free(u64 self,u64 value) {
    assert(self==reinterpret_cast<u64>(heap.data()) && value==descriptor);
    assert(load<u64>(value+0x60)==0); // CloseWindow precedes heap free
    std::free(reinterpret_cast<void*>(value));descriptor=0;++frees;return 0;
}
u64 __attribute__((sysv_abi)) Open(u64 d) {
    assert(!descriptor);descriptor=d;
    assert(load<u64>(d)==0x5736b30 && load<u64>(d+8)==0x5736b70);
    assert(load<u64>(d+0x18)==6 && load<u64>(d+0x20)==0x4d98486);
    assert(load<u64>(d+0x30)==0x573ee10 && load<u64>(d+0x38)==0x1f08010);
    assert(load<u64>(d+0x50)==d+0x30 && load<u64>(d+0x68)==0x57310d0);
    if (drop_game_reference) assert(BbMenuSteps::Release(current_step));
    store<u64>(d+0x60,fail_window ? 0 : reinterpret_cast<u64>(controller.data()));
    return 0;
}
u64 __attribute__((sysv_abi)) Close(u64 d) {
    assert(d==descriptor);store<u64>(d+0x60,0);++closes;return 0;
}
u64 __attribute__((sysv_abi)) Update(u64 self,u64 active) {
    assert(self==reinterpret_cast<u64>(controller.data()) && load<u8>(active)==1);
    ++updates;return 0;
}
void __attribute__((sysv_abi)) Destroy(u64) { ++destroyed; }
}
int main() {
    if (!eboot_kit::load()) return 77;
    auto* image=eboot_kit::image;
    unsetenv("BB_CHANGE_APPEARANCE");
    assert(!BbAppearance::Install(image,0x6000000) && !BbAppearance::Read().enabled);
    setenv("BB_CHANGE_APPEARANCE","1",1);
    assert(!BbAppearance::Install(image,32));
    // Guard every entry and the original window name before installing hooks.
    for (u64 at : {0x1ed8150ull,0x1ed8e60ull,0x1f08010ull,0x4d98486ull}) {
        auto* code=eboot_kit::at(at);code[0]^=1;
        assert(!BbAppearance::Install(image,0x6000000));
        code[0]^=1;
    }
    auto* pacer=eboot_kit::at(0x2434770);pacer[0]^=1;
    assert(!BbAppearance::Install(image,0x6000000) && !BbAppearance::Read().enabled);
    pacer[0]^=1; // a failed subscription must not leave a duplicate menu watch
    assert(BbAppearance::Install(image,0x6000000) && BbAppearance::Read().enabled);
    assert(BbAppearance::Install(image,0x6000000));
    assert(!BbAppearance::Install(image+16,0x6000000));
    eboot_kit::stub(0x1ed8150,reinterpret_cast<void*>(&Open));
    eboot_kit::stub(0x1ed8e60,reinterpret_cast<void*>(&Close));
    heap_table[0x58/8]=reinterpret_cast<u64>(&Allocate);
    heap_table[0x70/8]=reinterpret_cast<u64>(&Free);
    store<u64>(reinterpret_cast<u64>(heap.data()),reinterpret_cast<u64>(heap_table.data()));
    store<u64>(0x5940418,reinterpret_cast<u64>(heap.data()));
    controller_table[3]=reinterpret_cast<u64>(&Update);
    store<u64>(reinterpret_cast<u64>(controller.data()),reinterpret_cast<u64>(controller_table.data()));
    step_table[0]=reinterpret_cast<u64>(&Destroy);
    alignas(16) std::array<u8,0x70> world{};
    alignas(16) std::array<u8,0x400> player{};
    const auto world_at=reinterpret_cast<u64>(world.data()),player_at=reinterpret_cast<u64>(player.data());
    store<u64>(0x593e878,world_at);store<u64>(world_at+0x60,player_at);
    store<u32>(player_at+0x3f8,0x15000000);
    assert(mprotect(reinterpret_cast<void*>(0x2434000),4096,PROT_READ|PROT_WRITE|PROT_EXEC)==0);
    constexpr u8 unwind[]{0x48,0x83,0xc4,0x38,0x5b,0x41,0x5c,0x41,0x5d,0x41,0x5e,0x41,0x5f,0x5d,0xc3};
    std::memcpy(eboot_kit::at(0x2434781),unwind,sizeof unwind);
    const auto frame=eboot_kit::fn<void (*)()>(0x2434770);
    using Ctor=void (*)(void*,u64,u64,const char16_t*,void*,void*,void*);
    const auto ctor=eboot_kit::fn<Ctor>(0x201cce0);
    alignas(16) std::array<u8,0x30> factory{};
    alignas(16) std::array<u8,0x100> a{},b{},c{};
    const auto make=[&](auto& object) {
        ctor(object.data(),0,7,u"ChrMakeCommandList",factory.data(),factory.data(),factory.data());
        current_step=reinterpret_cast<u64>(object.data());
        store<u64>(current_step,reinterpret_cast<u64>(step_table.data()));
        store<std::int32_t>(current_step+8,1); // the game's menu owns it after construction
    };
    frame(); // establishes main-thread owner without a pending step
    make(a);
    std::thread worker([&] { frame(); });worker.join();
    assert(!allocations); // service calls cannot steal the main thread's pending menu
    frame();
    assert(allocations==1 && updates==1 && BbMenuSteps::Count(current_step)==2);
    frame();assert(allocations==1 && updates==2);
    assert(BbMenuSteps::Release(reinterpret_cast<u64>(a.data())));
    make(b);frame(); // previous list closed, new list opened in this same tick
    assert(allocations==2 && closes==1 && destroyed==1 && updates==3);
    store<u64>(world_at+0x60,0);frame(); // world teardown closes its preview
    assert(closes==2 && frees==2 && BbMenuSteps::Count(current_step)==1);
    assert(BbMenuSteps::Release(current_step) && destroyed==2);
    store<u64>(world_at+0x60,player_at);
    fail_allocate=true;make(c);frame();
    assert(BbMenuSteps::Count(current_step)==1 && BbAppearance::Read().failed==1);
    assert(BbMenuSteps::Release(current_step));fail_allocate=false;
    fail_window=true;make(c);frame();
    assert(BbMenuSteps::Count(current_step)==1 && BbAppearance::Read().failed==2);
    assert(allocations==3 && closes==3 && frees==3 && !descriptor);
    assert(BbMenuSteps::Release(current_step));fail_window=false;
    // The window factory releases the menu's original reference. The early
    // feature hold keeps it alive until the frame closes and drops its own.
    drop_game_reference=true;make(c);frame();
    assert(!descriptor && BbMenuSteps::Count(current_step)==0);
    const auto stats=BbAppearance::Read();
    assert(stats.opened==3 && stats.closed==3 && stats.failed==2 && stats.updates==4);
    assert(allocations==4 && frees==4 && closes==4 && destroyed==5);
    std::puts("PASS: retail menu/pacer hooks, preview descriptor, early reference hold, main-thread owner, replacement, teardown and failure cleanup");
}
