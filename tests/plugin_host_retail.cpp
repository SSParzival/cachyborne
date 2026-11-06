// SPDX-License-Identifier: GPL-3.0-or-later
// Copied-save-only image-phase probe. No guest hooks, HLE replacements or tasks.
#include "plugins/bbhost_plugin.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
namespace { const BbHostApi* api=nullptr; }
extern "C" {
BB_PLUGIN_EXPORT extern const BbPluginInfo bb_plugin_info={1,0,"native-host-retail-test","Native host retail test","1","Cachyborne","Copied-save-only test"};
BB_PLUGIN_EXPORT int bb_plugin_init(const BbHostApi* a) {
    api=a;
    if (a->version!=1 || a->size!=offsetof(BbHostApi,on_frame) ||
        std::strcmp(a->data_dir(),a->config("expected_data")) || *a->mods_dir() ||
        *a->eboot_sha256() || a->eboot_is_109()) return 10;
    const int fd=open(a->config("proof"),O_WRONLY|O_CREAT|O_EXCL,0600);
    if (fd<0) return 11;
    FILE* file=fdopen(fd,"w");if (!file) { close(fd);return 12; }
    const bool written=std::fprintf(file,"init\t%u\t%u\t%s\n",a->version,a->size,a->data_dir())>0;
    return std::fclose(file) || !written?13:0;
}
BB_PLUGIN_EXPORT int bb_plugin_image(const BbHostApi*) {
    if (!api->eboot_is_109() || std::strcmp(api->eboot_sha256(),api->config("expected_sha"))) return 20;
    const auto data=std::strtoull(api->config("data"),nullptr,0);
    const auto code=std::strtoull(api->config("code"),nullptr,0);
    if (!data || !code || !api->guest_addr(data) || !api->guest_addr(code)) return 21;
    unsigned char instructions[14]{};
    std::uint32_t before=0,changed=0,observed=0,restored=0;
    if (api->read(code,instructions,sizeof(instructions)) || api->read(data,&before,sizeof(before))) return 22;
    changed=before^0x01020304u;
    if (api->write(data,&changed,sizeof(changed)) || api->read(data,&observed,sizeof(observed))) return 23;
    const int rollback=api->write(data,&before,sizeof(before));
    if (rollback || api->read(data,&restored,sizeof(restored)) || observed!=changed || restored!=before) return 24;
    if (api->guest_addr(data)!=data && api->write(api->guest_addr(data),&before,sizeof(before))!=1) return 25;
    if (api->read(api->guest_addr(data),&restored,sizeof(restored)) || restored!=before) return 26;
    FILE* file=std::fopen(api->config("proof"),"a");if (!file) return 27;
    const bool written=std::fprintf(file,"image\t%s\t%llu\t%u\t%u\t%u\t%u\n",
        api->eboot_sha256(),(unsigned long long)api->guest_addr(code),before,changed,observed,restored)>0;
    return std::fclose(file) || !written?28:0;
}
}
