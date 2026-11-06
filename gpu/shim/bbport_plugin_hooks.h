// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "bbport_plugin_image.h"
#include "plugins/bbhost_plugin.h"

namespace BbPluginHooks {
// BBHOST C-hook adapters backed by native conditional guest hooks. The host
// runs image phases before guest startup: these methods never suspend threads
// and are not a live-patching API. Only process-resident module contexts admit
// permanent trampolines. Retired/quarantined owners run the original function.
class Hooks {
public:
    explicit Hooks(const BbPluginImage::Image& image) : image(image) {}
    int Hook(std::uint64_t bn,const void* prologue,std::size_t bytes,BbHookFn callback,void* user);
    int Hook2(std::uint64_t bn,const void* prologue,std::size_t bytes,BbHookFn2 callback,void* user);
private:
    int Install(std::uint64_t,const void*,std::size_t,BbHookFn,BbHookFn2,void*);
    const BbPluginImage::Image& image;
};
}
