// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "bbport_plugin_callbacks.h"
#include "plugins/bbhost_plugin.h"
#include <memory>
#include <string>

namespace BbPluginModule {
enum class Residency {
    Process, // Default until the host tracks every hook/HLE/thread resource.
    CallbacksOnly // Explicit contract: the supplied API publishes only callbacks.
};
// Two-phase BBHOST module loader. The integrating host supplies its truthful
// service table, whose lifetime must cover all plugin activity. This loader
// does not advertise an implemented API version or manufacture service stubs.
// The registry must outlive the module and all dispatches. Phase calls and
// Close must be serialized by the host. Callbacks may dispatch concurrently.
class Module {
public:
    static std::unique_ptr<Module> Open(const std::string& path,
        BbPluginCallbacks::Registry&, const BbHostApi&, std::string& error,
        Residency residency = Residency::Process);
    ~Module();
    Module(const Module&) = delete;
    Module& operator=(const Module&) = delete;
    bool Image(std::string& error);
    // Disables and drains owned callbacks. In Process mode the DSO remains
    // resident, including on init/image failure; it is never force-dlclosed.
    bool Close();
    const std::string& Name() const { return name; }
    static int OnFrame(void (*callback)(void*), void* user);
    static int RegisterHle(const char* name,void* function);
    static bool CanInstallHooks();
private:
    Module(BbPluginCallbacks::Registry&, const BbHostApi&);
    bool Phase(int (*fn)(const BbHostApi*), const char* phase, std::string& error);
    BbPluginCallbacks::Registry& registry;
    const BbHostApi& api;
    BbPluginCallbacks::Registry::Owner owner;
    std::shared_ptr<void> pin;
    int (*image)(const BbHostApi*) = nullptr;
    bool image_called = false, closed = false;
    Residency residency = Residency::Process;
    std::uint64_t hle_owner = 0;
    int (*add_hle)(std::uint64_t,const char*,void*) = nullptr;
    void (*retire_hle)(std::uint64_t) = nullptr;
    std::string name;
};
}
