// SPDX-License-Identifier: GPL-3.0-or-later
#include "bbport_plugin_host.h"
#include "bbport_plugin_callbacks.h"
#include "bbport_plugin_hooks.h"
#include "bbport_plugin_module.h"
#include <atomic>
#include <cstdarg>
#include <cstddef>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <set>
#include <stdexcept>
#include <vector>

namespace BbPluginHost {
struct Host::State {
    using Registry=BbPluginCallbacks::Registry;
    struct Hosts {
        std::mutex mutex;
        std::map<Registry*,std::weak_ptr<State>> entries;
        std::vector<std::shared_ptr<State>> resident;
        std::weak_ptr<State> process;
    };
    static Hosts& All() { static auto* const hosts=new Hosts;return *hosts; }
    static std::shared_ptr<State> Lookup(Registry* registry) {
        auto& hosts=All();std::lock_guard lock{hosts.mutex};
        // BBHOST's read/config/data services are process services: raw HLE
        // replacements and plugin worker threads also call them outside a
        // registered hook. One immutable process host avoids cross-game rebinding.
        if (!registry) return hosts.process.lock();
        const auto found=hosts.entries.find(registry);
        return found==hosts.entries.end()?nullptr:found->second.lock();
    }
    struct Access {
        Registry::Context context=Registry::Current();
        std::shared_ptr<State> state=Lookup(context.registry);
        Registry::Invocation invocation{context.owner};
        bool entered=false;
        Access() {
            if (!state || (context.registry && !invocation)) return;
            std::lock_guard lock{state->calls_mutex};
            if (!state->active.load(std::memory_order_acquire)) return;
            ++state->calls;++api_depth;entered=true;
        }
        ~Access() {
            if (!entered) return;
            --api_depth;
            std::lock_guard lock{state->calls_mutex};
            if (!--state->calls) state->calls_drained.notify_all();
        }
        explicit operator bool() const { return entered; }
    };
    template<class Result,class Function>
    static Result With(Result fallback,Function function) noexcept {
        try { Access access;if (access) return function(*access.state); }
        catch (...) {} // Native failures never unwind through a C plugin API.
        return fallback;
    }
    static void Log(const char* format,...) noexcept {
        if (!format) return;
        va_list arguments;va_start(arguments,format);
        With(0,[&](State&) {
            std::fputs("Plugin: ",stdout);std::vfprintf(stdout,format,arguments);
            std::fputc('\n',stdout);return 0;
        });
        va_end(arguments);
    }
    static const char* Config(const char* key) noexcept {
        return With<const char*>("",[&](State& s) {
            if (!key) return "";
            const auto found=s.configuration.values.find(key);
            return found==s.configuration.values.end()?"":found->second.c_str();
        });
    }
    static const char* DataDir() noexcept {
        return With<const char*>("",[](State& s) { return s.configuration.data_dir.c_str(); });
    }
    static const char* ModsDir() noexcept {
        return With<const char*>("",[](State& s) { return s.configuration.mods_dir.c_str(); });
    }
    static int RegisterHle(const char* name,void* function) noexcept {
        return With(1,[&](State&) { return BbPluginModule::Module::RegisterHle(name,function); });
    }
    static const char* Sha256() noexcept {
        return With<const char*>("",[](State& s) {
            const auto guest=s.guest.load(std::memory_order_acquire);
            return guest?guest->image->Sha256().c_str():"";
        });
    }
    static int Is109() noexcept {
        return With(0,[](State& s) {
            const auto guest=s.guest.load(std::memory_order_acquire);
            return int(guest && guest->image->Is109());
        });
    }
    static std::uint64_t GuestAddress(std::uint64_t bn) noexcept {
        return With<std::uint64_t>(0,[&](State& s) {
            const auto guest=s.guest.load(std::memory_order_acquire);
            return guest?guest->image->GuestAddress(bn):0;
        });
    }
    static int Read(std::uint64_t address,void* out,std::size_t bytes) noexcept {
        return With(1,[&](State& s) {
            const auto guest=s.guest.load(std::memory_order_acquire);
            return guest && guest->image->Read(address,out,bytes)?0:1;
        });
    }
    static int Write(std::uint64_t bn,const void* bytes,std::size_t count) noexcept {
        return With(1,[&](State& s) {
            const auto guest=s.guest.load(std::memory_order_acquire);
            if (!guest || !guest->image->GuestAddress(bn,count)) return 1;
            if (!bytes || !count) return 2;
            const auto result=guest->image->Write(bn,{static_cast<const std::uint8_t*>(bytes),count},
                BbPluginModule::Module::CanInstallHooks());
            return result==BbGuestPatch::Result::Applied || result==BbGuestPatch::Result::AlreadyApplied?0:2;
        });
    }
    static int Patch(std::uint64_t bn,const void* expected,const void* bytes,std::size_t count) noexcept {
        return With(1,[&](State& s) {
            const auto guest=s.guest.load(std::memory_order_acquire);
            if (!guest || !guest->image->GuestAddress(bn,count)) return 1;
            if (!BbPluginModule::Module::CanInstallHooks() || !bytes || !expected || !count) return 2;
            const auto result=guest->image->Patch(bn,{static_cast<const std::uint8_t*>(expected),count},
                {static_cast<const std::uint8_t*>(bytes),count});
            if (result==BbGuestPatch::Result::Mismatch) return 1;
            return result==BbGuestPatch::Result::Applied || result==BbGuestPatch::Result::AlreadyApplied?0:2;
        });
    }
    static int Hook(std::uint64_t bn,const void* prologue,std::size_t bytes,BbHookFn callback,void* user) noexcept {
        return With(1,[&](State& s) {
            const auto guest=s.guest.load(std::memory_order_acquire);
            return guest?guest->hooks.Hook(bn,prologue,bytes,callback,user):1;
        });
    }
    static BbHostApi Table() {
        BbHostApi api{};
        api.version=1;api.size=offsetof(BbHostApi,on_frame);
        api.log=Log;api.config=Config;api.data_dir=DataDir;api.mods_dir=ModsDir;
        api.register_hle=RegisterHle;api.eboot_sha256=Sha256;api.eboot_is_109=Is109;
        api.guest_addr=GuestAddress;api.read=Read;api.write=Write;api.patch=Patch;api.hook=Hook;
        return api;
    }
    explicit State(Configuration configuration) : configuration(std::move(configuration)) {}
    Configuration configuration;
    Registry registry;
    BbHostApi api=Table();
    struct Guest {
        std::unique_ptr<BbPluginImage::Image> image;
        BbPluginHooks::Hooks hooks;
        explicit Guest(std::unique_ptr<BbPluginImage::Image> image)
            : image(std::move(image)),hooks(*this->image) {}
    };
    std::atomic<std::shared_ptr<Guest>> guest;
    std::vector<std::unique_ptr<BbPluginModule::Module>> modules;
    std::set<std::filesystem::path> paths;
    std::atomic<bool> active{true};
    std::mutex calls_mutex;
    std::condition_variable calls_drained;
    unsigned calls=0;
    inline static thread_local unsigned api_depth=0;
    bool image_started=false,pinned=false;
};

Host::Host(Configuration configuration) : state(std::make_shared<State>(std::move(configuration))) {
    auto& hosts=State::All();std::lock_guard lock{hosts.mutex};
    if (!hosts.process.expired()) throw std::logic_error("native plugin host already owns this process");
    hosts.entries.emplace(&state->registry,state);
    hosts.process=state;
}
Host::~Host() { try { Close(); } catch (...) {} }
const BbHostApi& Host::Api() const { return state->api; }
bool Host::Load(const std::string& path,std::string& error) {
    error.clear();
    if (!state->active || state->image_started) { error="plugin init phase is closed";return false; }
    std::error_code ec;
    const auto canonical=std::filesystem::canonical(path,ec);
    if (ec) { error="cannot resolve plugin path: "+ec.message();return false; }
    if (state->paths.contains(canonical)) { error="plugin path is already loaded";return false; }
    if (!state->pinned) {
        auto& hosts=State::All();std::lock_guard lock{hosts.mutex};
        hosts.resident.push_back(state);state->pinned=true;
    }
    auto module=BbPluginModule::Module::Open(canonical.string(),state->registry,state->api,error);
    if (!module) return false;
    state->paths.insert(canonical);
    state->modules.push_back(std::move(module));
    return true;
}
bool Host::Image(std::unique_ptr<BbPluginImage::Image> image,std::string& error) {
    error.clear();
    if (!state->active || state->image_started || !image) { error="plugin image phase unavailable";return false; }
    state->image_started=true;
    state->guest.store(std::make_shared<State::Guest>(std::move(image)),std::memory_order_release);
    for (auto& module:state->modules) {
        if (!module->Image(error)) { error=module->Name()+": "+error;Close();return false; }
    }
    return true;
}
bool Host::Close() {
    {
        std::lock_guard lock{state->calls_mutex};
        state->active.store(false,std::memory_order_release);
    }
    {
        auto& hosts=State::All();std::lock_guard lock{hosts.mutex};
        hosts.entries.erase(&state->registry);
    }
    bool drained=true;
    for (auto& module:state->modules) if (!module->Close()) drained=false;
    if (State::api_depth) return false;
    {
        std::unique_lock lock{state->calls_mutex};
        state->calls_drained.wait(lock,[&] { return !state->calls; });
    }
    return drained;
}
}
