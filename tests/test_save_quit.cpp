// SPDX-License-Identifier: GPL-2.0-or-later
// Execute the complete retail question factory and affirmative callback.
// UI construction/imports are controlled; both original functor clones run.
#include "retail_timing_kit.h"
#include "games/bloodborne/save_quit.h"
#include <array>
#include <atomic>
#include <thread>

namespace {
std::atomic<unsigned> confirmed{0};
std::array<u8,16> functor{},second_clone{};
std::array<u64,3> holder{};
unsigned constructions=0;
bool expected_asking=false;
void Confirm() { confirmed.fetch_add(1); }
void* MakeText(void* out,u32 category,u32 id) {
    assert(category==0xcc && id==110000);
    assert(BbSaveQuit::Asking()==expected_asking);
    std::memset(out,0,0x40);return out;
}
void* DialogData(void* out,void*,void*) { std::memset(out,0,0x40);return out; }
void* ToFunction(void** out,void* source,u32 mode) {
    assert(mode==2);
    using Clone=void*(*)(void*,void*);
    assert(eboot_kit::fn<Clone>(0x200cfd0)(source,functor.data())==functor.data());
    assert(eboot_kit::fn<Clone>(0x200d010)(functor.data(),second_clone.data())==second_clone.data());
    holder[1]=2; // one retained host owner plus the factory's temporary owner
    *out=holder.data();return out;
}
void* CreateDialog(void* out,void*,void* callback) {
    assert(*static_cast<void**>(callback)==holder.data());
    ++constructions;return out;
}
}
int main() {
    if (!eboot_kit::load()) return 77;
    auto* image=eboot_kit::image;
    assert(!BbSaveQuit::Available());
    assert(!BbSaveQuit::OpenQuestion(nullptr,nullptr));
    assert(!BbSaveQuit::Install(nullptr,0,Confirm));
    assert(!BbSaveQuit::Install(image,32,Confirm));
    for (u64 address : {0x200ca4dull,0x200d0c4ull,0x200cffaull,0x200d03aull}) {
        auto* code=eboot_kit::at(address);code[0]^=1;
        assert(!BbSaveQuit::Install(image,0x6000000,Confirm));
        assert(!BbSaveQuit::Available());code[0]^=1;
    }
    const u64 invoke=load<u64>(0x5744a20);
    store<u64>(0x5744a20,invoke+1);
    assert(!BbSaveQuit::Install(image,0x6000000,Confirm));
    store<u64>(0x5744a20,invoke);
    assert(BbSaveQuit::Install(image,0x6000000,Confirm));
    assert(BbSaveQuit::Install(image,0x6000000,Confirm));
    assert(!BbSaveQuit::Install(image+16,0x6000000,Confirm));
    eboot_kit::stub(0x1ee8cc0,reinterpret_cast<void*>(&MakeText));
    eboot_kit::stub(0x201ed10,reinterpret_cast<void*>(&DialogData));
    eboot_kit::stub(0x1d5db60,reinterpret_cast<void*>(&ToFunction));
    eboot_kit::stub(0x201a070,reinterpret_cast<void*>(&CreateDialog));
    std::array<u64,2> manager{};
    std::array<u8,0x300> menu{};
    store<u64>(0x593b130,reinterpret_cast<u64>(manager.data()));
    store<u64>(0x5962878,reinterpret_cast<u64>(menu.data()));
    std::array<u8,32> out{},arg{};
    expected_asking=true;
    assert(BbSaveQuit::OpenQuestion(out.data(),arg.data())==out.data());
    assert(!BbSaveQuit::Asking() && confirmed==0);
    assert(functor[8]==1 && second_clone[8]==1);
    // Cancelling means the affirmative functor is never invoked. A later
    // normal question must not inherit a global pending desktop-exit intent.
    expected_asking=false;
    using Factory=void*(*)(void*,void*);
    assert(eboot_kit::fn<Factory>(0x200c9f0)(out.data(),arg.data())==out.data());
    assert(functor[8]==0 && second_clone[8]==0 && confirmed==0);
    using Invoke=void(*)(void*);
    auto yes=eboot_kit::fn<Invoke>(0x200d050);
    yes(second_clone.data());
    assert(confirmed==0 && load<u32>(reinterpret_cast<u64>(menu.data())+0x250)==1);
    menu.fill(0);
    expected_asking=true;
    assert(BbSaveQuit::OpenQuestion(out.data(),arg.data())==out.data());
    std::thread other_thread([&] {
        assert(!BbSaveQuit::Asking());
        yes(second_clone.data());
        yes(second_clone.data());
    });
    other_thread.join();
    assert(confirmed==1 && second_clone[8]==0);
    std::array<u8,0x300> expected{};
    expected[0x250]=1;
    assert(menu==expected); // original callback's state writes are preserved
    assert(constructions==3 && !BbSaveQuit::Asking());
    std::puts("PASS: retail question factory, both functor clones, cancellation then ordinary Quit Game, confirmed-only desktop intent on another thread and original callback state");
}
