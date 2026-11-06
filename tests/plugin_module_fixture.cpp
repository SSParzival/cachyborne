// SPDX-License-Identifier: GPL-3.0-or-later
#include "plugins/bbhost_plugin.h"
#include <cstring>
#include <stdexcept>
namespace {
const BbHostApi* api;
struct FixtureError : std::runtime_error {
    FixtureError(const char* message) : std::runtime_error(message) {}
    ~FixtureError() override { api->log("exception-destroyed"); }
};
void Later(void*) { api->log("later"); }
void Frame(void*) {
    api->log("frame");
    if (!std::strcmp(api->config("case"),"hle-frame-throw")) throw FixtureError("fixture frame");
    // A registration from a running plugin callback must retain its owner.
    api->on_frame(Later,nullptr);
    if (!std::strncmp(api->config("case"),"hle-",4) &&
        !api->register_hle("sceKernelUsleep",reinterpret_cast<void*>(Frame)))
        api->log("unexpected late HLE registration");
}
int32_t BB_GUEST_ABI Replacement(uint32_t value) { return 444+static_cast<int32_t>(value); }
}
extern "C" {
BB_PLUGIN_EXPORT extern const BbPluginInfo bb_plugin_info={2,0,"lifecycle-test","Lifecycle test","1","Cachyborne","Test fixture"};
BB_PLUGIN_EXPORT int bb_plugin_init(const BbHostApi* a) {
    api=a;
    if (api->on_frame(Frame,nullptr)) return 19;
    if (!std::strncmp(api->config("case"),"hle-",4)) {
        if (api->register_hle("sceKernelUsleep",reinterpret_cast<void*>(Replacement))) return 20;
        if (!std::strcmp(api->config("case"),"hle-init-fail")) return 7;
    }
    if (!std::strcmp(api->config("case"),"init-fail")) return 7;
    if (!std::strcmp(api->config("case"),"init-throw")) throw FixtureError("fixture init");
    return 0;
}
BB_PLUGIN_EXPORT int bb_plugin_image(const BbHostApi*) {
    if (!std::strncmp(api->config("case"),"hle-",4)) {
        if (!api->register_hle("sceKernelUsleep",reinterpret_cast<void*>(Replacement))) return 21;
        if (!std::strcmp(api->config("case"),"hle-image-fail")) return 8;
    }
    if (!std::strcmp(api->config("case"),"image-fail")) return 8;
    if (!std::strcmp(api->config("case"),"image-throw")) throw FixtureError("fixture image");
    api->log("image");return 0;
}
}
