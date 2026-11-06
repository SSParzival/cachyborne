// SPDX-License-Identifier: GPL-3.0-or-later
#include "bbport_plugin_module.h"
#include <array>
#include <cstddef>
#include <dlfcn.h>
#include <exception>
#include <string_view>
#include <mutex>
#include <vector>
#include "bbgpu.h"

namespace BbPluginModule {
namespace {
std::mutex backend_mutex;
int (*backend_add)(std::uint64_t,const char*,void*) = nullptr;
void (*backend_retire)(std::uint64_t) = nullptr;
std::atomic<std::uint64_t> next_owner{1};
struct HleCleanup { void (*retire)(std::uint64_t);std::uint64_t owner; };
void RetireHle(void* context) noexcept {
    const auto& cleanup=*static_cast<const HleCleanup*>(context);
    cleanup.retire(cleanup.owner);
}
thread_local Module* initializing = nullptr;
thread_local Module* imaging = nullptr;
struct InitScope {
    Module* previous;
    Module* previous_image;
    InitScope(Module* current,Module* image) : previous(initializing),previous_image(imaging) {
        initializing=current;imaging=image;
    }
    ~InitScope() { initializing=previous;imaging=previous_image; }
};
// BBHOST appends fields at each API revision; never admit a plugin against
// a table whose advertised version extends beyond the supplied allocation.
constexpr std::array<std::size_t,16> ApiSizes{
    offsetof(BbHostApi,on_frame), offsetof(BbHostApi,event_flag_get),
    offsetof(BbHostApi,on_event_flag), offsetof(BbHostApi,player_position),
    offsetof(BbHostApi,player_block), offsetof(BbHostApi,lamp_warp),
    offsetof(BbHostApi,symbol), offsetof(BbHostApi,on_option),
    offsetof(BbHostApi,set_rules), offsetof(BbHostApi,replace_text),
    offsetof(BbHostApi,loading_screen), offsetof(BbHostApi,input),
    offsetof(BbHostApi,show_text), offsetof(BbHostApi,on_scene_category),
    offsetof(BbHostApi,draw_style), sizeof(BbHostApi)};
void KeepResident(const std::shared_ptr<void>& pin) {
    // Exit destructors must not dlclose code still referenced by game workers.
    struct Resident { std::mutex mutex;std::vector<std::shared_ptr<void>> pins; };
    static auto* const resident = new Resident;
    std::lock_guard lock{resident->mutex};
    resident->pins.push_back(pin);
}
}
Module::Module(BbPluginCallbacks::Registry& r,const BbHostApi& a) : registry(r),api(a) {}
Module::~Module() {
    // If snapshot publication runs out of memory, Retire has already closed
    // admission. Its old entries/snapshots still pin any remaining callbacks.
    try { Close(); } catch (...) {}
}

std::unique_ptr<Module> Module::Open(const std::string& path,
    BbPluginCallbacks::Registry& registry,const BbHostApi& api,std::string& error,Residency residency) {
    error.clear();
    if (!api.version || api.version>ApiSizes.size() || api.size<ApiSizes[api.version-1]) {
        error="invalid host API version/size";return {};
    }
    void* handle=dlopen(path.c_str(),RTLD_NOW|RTLD_LOCAL);
    if (!handle) { error=dlerror();return {}; }
    auto pin=std::shared_ptr<void>(handle,[](void* h) { dlclose(h); });
    // Even constructors can start work. Process policy pins the loaded image
    // before executing phase functions, including all subsequent error paths.
    if (residency==Residency::Process) KeepResident(pin);
    const auto init=reinterpret_cast<int (*)(const BbHostApi*)>(dlsym(handle,"bb_plugin_init"));
    const auto image=reinterpret_cast<int (*)(const BbHostApi*)>(dlsym(handle,"bb_plugin_image"));
    const auto* info=static_cast<const BbPluginInfo*>(dlsym(handle,"bb_plugin_info"));
    if (!init && !image) { error="no bb_plugin_init or bb_plugin_image";return {}; }
    if (info && (!info->api_version || info->api_version>api.version)) {
        error="plugin requires an unsupported API version";return {};
    }
    auto module=std::unique_ptr<Module>(new Module(registry,api));
    {
        std::lock_guard lock{backend_mutex};
        module->add_hle=backend_add;module->retire_hle=backend_retire;
    }
    module->hle_owner=next_owner.fetch_add(1);
    module->residency=residency;
    module->pin=std::move(pin);
    BbPluginCallbacks::Registry::DisableAction cleanup;
    if (module->retire_hle && residency==Residency::Process) {
        cleanup.run=RetireHle;
        cleanup.context=std::make_shared<HleCleanup>(HleCleanup{module->retire_hle,module->hle_owner});
    }
    module->owner=registry.CreateOwner(module->pin,std::move(cleanup));
    module->name=info && info->name ? info->name : path;
    module->image=image;
    if (!module->Phase(init,"init",error)) return {};
    return module;
}

bool Module::Phase(int (*fn)(const BbHostApi*),const char* phase,std::string& error) {
    if (!fn) return true;
    // Keep exception destructors/type information from the DSO resident until
    // the catch handler has finished, even if Close releases the module owner.
    const auto phase_pin=pin;
    int result=0;
    try {
        InitScope init_context{std::string_view(phase)=="init" ? this : nullptr,
            std::string_view(phase)=="image" ? this : nullptr};
        BbPluginCallbacks::Registry::Scope context{registry,owner};
        result=fn(&api);
    } catch (const std::exception& e) {
        error=std::string(phase)+" threw: "+e.what();Close();return false;
    } catch (...) {
        error=std::string(phase)+" threw";Close();return false;
    }
    if (result) {
        error=std::string(phase)+" returned "+std::to_string(result);
        Close();return false;
    }
    return true;
}
bool Module::Image(std::string& error) {
    error.clear();
    if (closed || image_called) { error="image phase unavailable or already called";return false; }
    image_called=true;
    return Phase(image,"image",error);
}
bool Module::Close() {
    if (closed) return true;
    if (!registry.Retire(owner)) return false;
    closed=true;
    owner={};pin.reset();
    return true;
}
int Module::OnFrame(void (*callback)(void*),void* user) {
    return BbPluginCallbacks::Registry::SubscribeCurrent(callback,user) ? 0 : 1;
}
int Module::RegisterHle(const char* name,void* function) {
    auto* module=initializing;
    if (!module || !module->add_hle || module->residency!=Residency::Process || !module->hle_owner)
        return 1;
    return module->add_hle(module->hle_owner,name,function);
}
bool Module::CanInstallHooks() {
    return imaging && !imaging->closed && imaging->residency==Residency::Process;
}
}

extern "C" int bbgpu_plugin_hle_backend_install(int (*add)(uint64_t,const char*,void*),void (*retire)(uint64_t)) {
    if (!add || !retire) return 1;
    std::lock_guard lock{BbPluginModule::backend_mutex};
    if (BbPluginModule::backend_add &&
        (BbPluginModule::backend_add!=add || BbPluginModule::backend_retire!=retire)) return 1;
    BbPluginModule::backend_add=add;BbPluginModule::backend_retire=retire;
    return 0;
}
