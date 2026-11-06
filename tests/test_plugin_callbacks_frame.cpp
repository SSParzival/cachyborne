// SPDX-License-Identifier: GPL-2.0-or-later
#include "retail_timing_kit.h"
#include "shim/bbport_plugin_callbacks.h"
#include <atomic>

namespace {
void Count(void* p) { ++*static_cast<std::atomic<unsigned>*>(p); }
}
int main() {
    if (!eboot_kit::load()) return 77;
    auto* image=eboot_kit::image;
    assert(!BbPluginCallbacks::InstallFrames(image,32));
    auto* entry=eboot_kit::at(0x2434770);
    entry[0]^=1;
    assert(!BbPluginCallbacks::InstallFrames(image,0x6000000));
    entry[0]^=1;
    auto& registry=BbPluginCallbacks::Frames();
    auto owner=registry.CreateOwner(std::make_shared<int>(1));
    std::atomic<unsigned> calls{0};
    assert(registry.Subscribe(owner,Count,&calls));
    assert(BbPluginCallbacks::InstallFrames(image,0x6000000));
    assert(BbPluginCallbacks::InstallFrames(image,0x6000000));
    assert(mprotect(reinterpret_cast<void*>(0x2434000),4096,
                    PROT_READ|PROT_WRITE|PROT_EXEC)==0);
    // Execute the real entry and observer trampoline; replace only the later
    // pacer body, whose game singleton/limiter are irrelevant to dispatch.
    constexpr u8 unwind[]{0x48,0x83,0xc4,0x38,0x5b,0x41,0x5c,0x41,0x5d,
                          0x41,0x5e,0x41,0x5f,0x5d,0xc3};
    std::memcpy(eboot_kit::at(0x2434781),unwind,sizeof unwind);
    const auto frame=eboot_kit::fn<void (*)()>(0x2434770);
    frame();assert(calls==1);
    assert(registry.Retire(owner));
    frame();assert(calls==1);
}
