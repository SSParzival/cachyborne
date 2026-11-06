// SPDX-License-Identifier: GPL-3.0-or-later
#include "plugins/bbhost_plugin.h"
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
namespace {
const BbHostApi* api=nullptr;
const char* saved_path=nullptr;
unsigned hook_calls=0;
std::uint64_t Number(const char* name) { return std::strtoull(api->config(name),nullptr,0); }
bool Case(const char* name) { return !std::strcmp(api->config("case"),name); }
std::int32_t BB_GUEST_ABI Sleep(std::uint32_t value) { return std::int32_t(value+Number("number")); }
int Hook(const BbHookFrame* frame,void*) {
    ++hook_calls;
    if (!frame || frame->rdi!=7) throw std::runtime_error("bad hook arguments");
    const auto data=Number("data"),code=Number("code");
    std::uint32_t before=0;
    if (api->read(data,&before,sizeof(before)) || before<123 || before>10000)
        throw std::runtime_error("owned live read failed");
    const auto after=before+1;
    if (
        api->write(data,&after,sizeof(after)) || Number("number")!=77)
        throw std::runtime_error("owned live services failed");
    const unsigned char nop=0x90;
    if (api->write(code,&nop,1)!=2 || api->patch(code,&nop,&nop,1)!=2 ||
        !api->register_hle("sceKernelUsleep",reinterpret_cast<void*>(Sleep)))
        throw std::runtime_error("late code/import mutation admitted");
    if (Case("hook-throw")) throw std::runtime_error("fixture hook");
    return 0;
}
}
extern "C" {
BB_PLUGIN_EXPORT extern const BbPluginInfo bb_plugin_info={1,0,"native-host-test","Native host test","1","Cachyborne","Test fixture"};
BB_PLUGIN_EXPORT const BbHostApi* bb_fixture_api() { return api; }
BB_PLUGIN_EXPORT const char* bb_fixture_path() { return saved_path; }
BB_PLUGIN_EXPORT unsigned bb_fixture_hook_calls() { return hook_calls; }
BB_PLUGIN_EXPORT int bb_plugin_init(const BbHostApi* a) {
    api=a;hook_calls=0;
    if (a->version!=1 || a->size!=offsetof(BbHostApi,on_frame) ||
        !a->log || !a->config || !a->data_dir || !a->mods_dir || !a->register_hle ||
        !a->eboot_sha256 || !a->eboot_is_109 || !a->guest_addr || !a->read || !a->write || !a->patch || !a->hook) return 10;
    a->log("native table v%u, value %u",a->version,unsigned(Number("number")));
    saved_path=a->data_dir();
    if (std::strcmp(saved_path,a->config("expected_data")) ||
        std::strcmp(a->mods_dir(),a->config("expected_mods")) ||
        *a->config("missing") || *a->config(nullptr) || Number("number")!=77) return 11;
    unsigned char value=0;
    if (*a->eboot_sha256() || a->eboot_is_109() || a->guest_addr(0x400000) ||
        !a->read(0x400000,&value,1) || !a->write(0x400000,&value,1) ||
        !a->patch(0x400000,&value,&value,1) || !a->hook(0x400000,&value,1,Hook,nullptr)) return 12;
    if (a->register_hle("sceKernelUsleep",reinterpret_cast<void*>(Sleep))) return 13;
    if (Case("init-fail")) return 7;
    if (Case("init-throw")) throw std::runtime_error("fixture init");
    return 0;
}
BB_PLUGIN_EXPORT int bb_plugin_image(const BbHostApi*) {
    if (Case("image-fail")) return 8;
    if (Case("image-throw")) throw std::runtime_error("fixture image");
    if (Case("hle-only")) return 0;
    if (std::strcmp(api->eboot_sha256(),api->config("expected_sha"))) return 20;
    if (Case("unknown-image")) {
        const unsigned char value=0;
        return api->eboot_is_109() || api->guest_addr(0x400000) ||
            api->write(0x400000,&value,1)!=1 || api->patch(0x400000,&value,&value,1)!=1 ? 21:0;
    }
    const auto data=Number("data"),code=Number("code");
    if (!api->eboot_is_109() || !api->guest_addr(code) ||
        !api->register_hle("sceKernelUsleep",reinterpret_cast<void*>(Sleep))) return 22;
    std::uint32_t value=123,read=0;
    if (api->write(data,&value,sizeof(value)) || api->read(data,&read,sizeof(read)) || read!=123 ||
        api->write(api->guest_addr(data),&value,sizeof(value))!=1) return 23;
    unsigned char prologue[14],changed[14],wrong[14];
    std::memset(prologue,0x90,sizeof(prologue));std::memcpy(changed,prologue,sizeof(changed));
    std::memset(wrong,0x44,sizeof(wrong));changed[0]=0x66;
    if (api->patch(code,prologue,changed,sizeof(prologue)) ||
        api->patch(code,prologue,changed,sizeof(prologue)) ||
        api->patch(code,wrong,prologue,sizeof(prologue))!=1 ||
        api->write(code,prologue,sizeof(prologue)) ||
        api->patch(code,prologue,prologue,sizeof(prologue)) ||
        api->hook(code,prologue,sizeof(prologue),Hook,nullptr)) return 24;
    return 0;
}
}
