// SPDX-License-Identifier: GPL-3.0-or-later
// Guarded integration with bb-probe's real pre-bind and pre-entry phases.
// Never installed by normal configuration or included in runtime distributions.
#include "bbport_plugin_host.h"
#include "common/sha256.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <elf.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>
namespace {
std::unique_ptr<BbPluginHost::Host> host;
std::vector<std::uint8_t> original;
std::filesystem::path marker;
std::uint64_t data=0;
void Require(bool condition,const char* message) {
    if (!condition) { std::fprintf(stderr,"Plugin host fixture: %s\n",message);std::abort(); }
}
std::string Text(const std::filesystem::path& path) {
    std::ifstream file(path);return {std::istreambuf_iterator<char>(file),{}};
}
}
extern "C" int bbgpu_plugin_hle_backend_install(int (*add)(std::uint64_t,const char*,void*),void (*retire)(std::uint64_t)) {
    using Install=int(*)(int(*)(std::uint64_t,const char*,void*),void(*)(std::uint64_t));
    auto next=reinterpret_cast<Install>(dlsym(RTLD_NEXT,"bbgpu_plugin_hle_backend_install"));
    Require(next && !next(add,retire),"native HLE backend unavailable");
    Require(!host,"init phase repeated");
    const char* guard=std::getenv("CACHYBORNE_PLUGIN_HOST_FIXTURE");
    const char* elf=std::getenv("CACHYBORNE_TEST_ELF");
    const char* module=std::getenv("CACHYBORNE_PLUGIN_HOST_MODULE");
    const char* user=std::getenv("BB_GPU_USER_DIR");
    Require(guard && elf && module && user,"explicit copied-save fixture environment missing");
    marker=guard;
    Require(marker.filename()==".copied-save-fixture" &&
        marker.parent_path().filename().string().starts_with("native-physical-plugin-host-v1-") &&
        Text(marker)=="cachyborne-plugin-host-copied-save-only\n","copied-save marker invalid");
    using UserDir=const char*(*)();auto actual=reinterpret_cast<UserDir>(dlsym(RTLD_DEFAULT,"runtime_file_user_dir"));
    Require(actual && std::filesystem::equivalent(user,marker.parent_path()/"capture/60/user") &&
        std::filesystem::equivalent(user,actual()),"runtime user directory is not the copied save tree");
    std::ifstream file(elf,std::ios::binary);original.assign(std::istreambuf_iterator<char>(file),{});
    Require(original.size()>=sizeof(Elf64_Ehdr),"original ELF missing");
    Elf64_Ehdr header;std::memcpy(&header,original.data(),sizeof(header));
    Require(!std::memcmp(header.e_ident,ELFMAG,SELFMAG) && header.e_phentsize==sizeof(Elf64_Phdr) &&
        header.e_phoff<=original.size() && header.e_phnum<=(original.size()-header.e_phoff)/sizeof(Elf64_Phdr),"original ELF metadata invalid");
    for (unsigned i=0;i<header.e_phnum;++i) {
        Elf64_Phdr segment;std::memcpy(&segment,original.data()+header.e_phoff+i*sizeof(segment),sizeof(segment));
        if (segment.p_type==PT_LOAD && (segment.p_flags&PF_W) && !(segment.p_flags&PF_X) && segment.p_memsz>=4) {
            data=0x400000+segment.p_vaddr;break;
        }
    }
    Require(data,"writable original data segment absent");
    host=std::make_unique<BbPluginHost::Host>(BbPluginHost::Configuration{
        .data_dir=user,.mods_dir="",.values={{"expected_data",user},
            {"expected_sha",sha256_hex(original.data(),original.size())},
            {"proof",(marker.parent_path()/"plugin-proof.tsv").string()},
            {"data",std::to_string(data)},{"code","0x2434770"}}});
    std::string error;const bool loaded=host->Load(module,error);Require(loaded,error.c_str());
    std::puts("Plugin host fixture: v1 init completed before native import binding");std::fflush(stdout);
    return 0;
}
extern "C" void bbgpu_patch_image(unsigned char* memory,std::uint64_t size) {
    using Patch=void(*)(unsigned char*,std::uint64_t);
    auto next=reinterpret_cast<Patch>(dlsym(RTLD_NEXT,"bbgpu_patch_image"));Require(next && host,"image phase without init");
    next(memory,size);
    auto image=BbPluginImage::Image::Create(original,memory,size);Require(bool(image) && image->Is109(),"actual mapped image is not 1.09");
    std::vector<std::uint8_t>().swap(original);
    std::string error;const bool initialized=host->Image(std::move(image),error);Require(initialized,error.c_str());
    const auto& api=host->Api();std::uint32_t before=0,changed=0,observed=0,restored=0;
    Require(!api.read(data,&before,sizeof(before)),"process-context read failed");
    changed=before^0x10203040u;
    Require(!api.write(data,&changed,sizeof(changed)),"writable-data API failed outside image callback");
    const int read=api.read(data,&observed,sizeof(observed));
    Require(!api.write(data,&before,sizeof(before)) && !api.read(data,&restored,sizeof(restored)) &&
        !read && observed==changed && restored==before,"live data round-trip or restoration failed");
    FILE* proof=std::fopen((marker.parent_path()/"plugin-proof.tsv").c_str(),"a");Require(proof,"proof unavailable");
    const bool written=std::fprintf(proof,"live\t%u\t%u\t%u\t%u\n",before,changed,observed,restored)>0;
    Require(!std::fclose(proof) && written,"proof publication failed");
    std::puts("Plugin host fixture: actual native image and live-data APIs verified; original data restored");std::fflush(stdout);
}
