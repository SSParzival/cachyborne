// SPDX-License-Identifier: GPL-3.0-or-later
// Loaded by the real runtime-test executable, whose resolver/backend callbacks
// are passed directly. The plugin, Module and import trampoline are all real.
#include "plugin_hle_integration.h"
#include "bbport_plugin_module.h"
#include "bbport_plugin_host.h"
#include "bbgpu.h"
#include <cassert>
#include <cstring>
#include <string>
namespace {
std::string scenario;
const char* Config(const char*) { return scenario.c_str(); }
void Log(const char* line,...) { assert(std::strcmp(line,"unexpected late HLE registration")); }
}
extern "C" int cachyborne_test_native_hle(const char* plugin,const char* test,const HleNativeBackend* backend) {
    using BbPluginModule::Module;
    assert(backend && !bbgpu_plugin_hle_backend_install(backend->add,backend->retire));
    scenario=test;
    if (scenario=="host-hle-success" || scenario=="host-hle-init-fail") {
        BbPluginHost::Host host({.data_dir="native-test-data",.mods_dir="native-test-mods",.values={
            {"case",scenario=="host-hle-success"?"hle-only":"init-fail"},{"number","77"},
            {"expected_data","native-test-data"},{"expected_mods","native-test-mods"}}});
        const auto original=backend->resolve("1jfXLRVzisc#p#J",0);assert(original);
        std::string error;const bool loaded=host.Load(plugin,error);
        assert(loaded==(scenario=="host-hle-success"));
        backend->seal();
        using Sleep=int32_t (*)(uint32_t);
        auto cached=reinterpret_cast<Sleep>(backend->resolve("1jfXLRVzisc#p#J",0));
        if (loaded) assert(reinterpret_cast<uintptr_t>(cached)!=original && cached(7)==84);
        else assert(reinterpret_cast<uintptr_t>(cached)==original && cached(0)==0);
        assert(host.Close() && cached(0)==0 && !*host.Api().config("number"));
        return 0;
    }
    BbHostApi api{};api.version=2;api.size=offsetof(BbHostApi,event_flag_get);
    api.log=Log;api.config=Config;api.register_hle=Module::RegisterHle;api.on_frame=Module::OnFrame;
    BbPluginCallbacks::Registry registry;
    const auto original=backend->resolve("1jfXLRVzisc#p#J",0);assert(original);
    std::string error;
    auto module=Module::Open(plugin,registry,api,error);
    backend->seal();
    using Sleep=int32_t (*)(uint32_t);
    Sleep cached=reinterpret_cast<Sleep>(backend->resolve("1jfXLRVzisc#p#J",0));
    if (scenario=="hle-init-fail") {
        assert(!module && reinterpret_cast<uintptr_t>(cached)==original && cached(0)==0);
    } else {
        assert(module && reinterpret_cast<uintptr_t>(cached)!=original && cached(7)==451);
        const bool image=module->Image(error);
        assert(image==(scenario!="hle-image-fail"));
        if (image) {
            if(scenario=="hle-frame-throw") {
                assert(registry.Dispatch()==1);
                assert(cached(0)==0); // restored synchronously by quarantine, before Close
            } else assert(!registry.Dispatch()); // nested frame registration belongs to this module
            assert(module->Close());
        }
        assert(cached(0)==0); // import pointer already cached, real native fallback
        assert(!registry.Dispatch());
    }
    return 0;
}
