// SPDX-License-Identifier: GPL-3.0-or-later
#include "bbport_plugin_module.h"
#include "bbgpu.h"
#include <atomic>
#include <cassert>
#include <cstring>
#include <dlfcn.h>
#include <string>

namespace {
std::string scenario;
std::atomic<unsigned> frames{0},later{0},images{0},exception_destructors{0};
std::uint64_t hle_owner=0;
void* hle_function=nullptr;
int AddHle(std::uint64_t owner,const char* name,void* function) {
    if (!owner || !name || std::strcmp(name,"sceKernelUsleep") || !function) return 1;
    hle_owner=owner;hle_function=function;return 0;
}
void RetireHle(std::uint64_t owner) {
    if (hle_owner==owner) { hle_owner=0;hle_function=nullptr; }
}
void OtherRetire(std::uint64_t) {}
const char* Config(const char*) { return scenario.c_str(); }
void Log(const char* text,...) {
    if (!std::strcmp(text,"frame")) ++frames;
    else if (!std::strcmp(text,"later")) ++later;
    else if (!std::strcmp(text,"image")) ++images;
    else if (!std::strcmp(text,"exception-destroyed")) ++exception_destructors;
    else assert(false);
}
bool Loaded(const char* path) {
    void* h=dlopen(path,RTLD_NOW|RTLD_NOLOAD);
    if (!h) return false;
    assert(!dlclose(h));return true;
}
}
int main(int argc,char** argv) {
    assert(argc==3);
    using BbPluginModule::Module;
    using BbPluginModule::Residency;
    BbPluginCallbacks::Registry registry;
    // Deliberately limited test API: the loader does not advertise this table
    // as a complete production implementation. Fixture uses only these fields.
    BbHostApi api{};api.version=2;api.size=offsetof(BbHostApi,event_flag_get);
    api.log=Log;api.config=Config;api.on_frame=Module::OnFrame;api.register_hle=Module::RegisterHle;
    std::string error;
    auto open=[&](const char* path) { return Module::Open(path,registry,api,error,Residency::CallbacksOnly); };
    assert(Module::OnFrame(nullptr,nullptr)==1);
    assert(!open("/cachyborne/no-such-plugin.so") && !error.empty());
    assert(!open(argv[2]) && error=="no bb_plugin_init or bb_plugin_image" && !Loaded(argv[2]));
    --api.size;
    assert(!open(argv[1]) && error=="invalid host API version/size" && !Loaded(argv[1]));
    ++api.size;
    api.version=1;api.size=offsetof(BbHostApi,on_frame);
    assert(!open(argv[1]) && error=="plugin requires an unsupported API version" && !Loaded(argv[1]));
    api.version=2;api.size=offsetof(BbHostApi,event_flag_get);
    for (const char* s:{"init-fail","init-throw"}) {
        scenario=s;
        assert(!open(argv[1]) && !error.empty() && !Loaded(argv[1]));
        assert(!registry.Dispatch() && frames==0 && later==0);
    }
    for (const char* s:{"image-fail","image-throw"}) {
        scenario=s;auto module=open(argv[1]);assert(module && Loaded(argv[1]));
        assert(!module->Image(error) && !error.empty() && !Loaded(argv[1]));
        assert(!registry.Dispatch() && frames==0 && later==0);
        assert(module->Close());
    }
    scenario="success";
    assert(exception_destructors==2);
    auto module=open(argv[1]);assert(module && error.empty());
    assert(module->Name()=="lifecycle-test" && module->Image(error));
    assert(!module->Image(error) && images==1);
    assert(!registry.Dispatch() && frames==1 && later==0);
    assert(!registry.Dispatch() && frames==2 && later==1);
    assert(module->Close() && module->Close() && !Loaded(argv[1]));
    assert(!registry.Dispatch() && frames==2 && later==1);
    // Destruction also removes callbacks without requiring an explicit Close.
    { auto temporary=open(argv[1]);assert(temporary); }
    assert(!Loaded(argv[1]) && !registry.Dispatch() && frames==2);
    // Module/backend lifecycle is tested here; real resolver/ABI trampolines
    // are independently covered by the native runtime C test.
    assert(bbgpu_plugin_hle_backend_install(nullptr,RetireHle));
    assert(!bbgpu_plugin_hle_backend_install(AddHle,RetireHle));
    assert(!bbgpu_plugin_hle_backend_install(AddHle,RetireHle));
    assert(bbgpu_plugin_hle_backend_install(AddHle,OtherRetire));
    assert(Module::RegisterHle("sceKernelUsleep",reinterpret_cast<void*>(AddHle)));
    scenario="hle-success";
    assert(!open(argv[1]) && !hle_owner); // callback-only policy cannot publish HLE
    scenario="hle-init-fail";
    assert(!Module::Open(argv[1],registry,api,error) && !hle_owner);
    scenario="hle-image-fail";
    { auto hle=Module::Open(argv[1],registry,api,error);assert(hle && hle_owner);
      assert(!hle->Image(error) && !hle_owner); }
    scenario="hle-success";
    { auto hle=Module::Open(argv[1],registry,api,error);assert(hle && hle_owner && hle_function);
      assert(reinterpret_cast<int32_t (*)(uint32_t)>(hle_function)(7)==451);
      assert(hle->Image(error));
      assert(!registry.Dispatch()); // HLE registration from frame is refused
      assert(hle->Close() && !hle_owner); }
    const auto final_frames=frames.load();
    // Default policy keeps a failed module resident, but its callbacks disabled.
    // Run last: this intentional process-lifetime reference cannot be unloaded.
    scenario="init-fail";
    assert(!Module::Open(argv[1],registry,api,error));
    assert(Loaded(argv[1]) && !registry.Dispatch() && frames==final_frames);
}
