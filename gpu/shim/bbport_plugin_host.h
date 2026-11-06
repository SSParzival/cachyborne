// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "bbport_plugin_image.h"
#include "plugins/bbhost_plugin.h"
#include <map>
#include <memory>
#include <string>

namespace BbPluginHost {
struct Configuration {
    std::string data_dir, mods_dir;
    std::map<std::string,std::string,std::less<>> values;
};
// Native service-table owner, built in BBHOST ABI order. The implemented
// prefix is version 1; later services are never advertised as null stubs.
// Load runs init before native import binding. Image runs after relocation and
// before any guest threads, with an identity made from the original ELF.
// The integrating runtime serializes Load/Image/Close. This is one game per
// process: after a load attempt, the guest image and native modules must remain
// mapped until process exit (raw HLE replacements/plugin threads can retain
// guest pointers). No discovery or task is installed by construction.
class Host {
public:
    explicit Host(Configuration);
    ~Host();
    Host(const Host&) = delete;
    Host& operator=(const Host&) = delete;
    bool Load(const std::string& path,std::string& error);
    bool Image(std::unique_ptr<BbPluginImage::Image>,std::string& error);
    // Disables services, drains owned hook/API calls and redirects HLE imports
    // to their native fallback. Does not drain raw HLE/plugin-created threads.
    // False inside a callback: retry outside dispatch. DSOs and returned strings
    // stay process-resident after a load attempt; a new game needs a new process.
    bool Close();
    const BbHostApi& Api() const;
private:
    struct State;
    std::shared_ptr<State> state;
};
}
