// SPDX-License-Identifier: GPL-3.0-or-later
#include "bbport_plugin_host.h"
#include "bbgpu.h"
#include <array>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <elf.h>
#include <fstream>
#include <future>
#include <iterator>
#include <semaphore>
#include <stdexcept>
#include <sys/mman.h>
#include <thread>
#include <unistd.h>
#include <vector>
using namespace std::chrono_literals;
namespace {
std::uint64_t hle_owner=0;
void* hle=nullptr;
int Add(std::uint64_t owner,const char* name,void* function) {
    assert(owner && name && !std::strcmp(name,"sceKernelUsleep") && function);
    hle_owner=owner;hle=function;return 0;
}
void Retire(std::uint64_t owner) { if (owner==hle_owner) { hle=nullptr;hle_owner=0; } }
struct Heap {
    std::uintptr_t address;
    BbPluginHost::Host* host=nullptr;
    bool block=false,self_close=false,inside_close=true;
    std::binary_semaphore entered{0},release{0};
};
bool Mapped(void* context,std::uintptr_t address,std::size_t size) {
    auto& heap=*static_cast<Heap*>(context);
    if (address<heap.address || address-heap.address>16 || size>16-(address-heap.address)) return false;
    if (heap.self_close) heap.inside_close=heap.host->Close();
    if (heap.block) { heap.entered.release();heap.release.acquire(); }
    return true;
}
std::vector<std::uint8_t> Synthetic() {
    std::vector<std::uint8_t> bytes(4096);
    Elf64_Ehdr header{};std::memcpy(header.e_ident,"\x7f" "ELF\x02\x01\x01",7);
    header.e_machine=EM_X86_64;header.e_phoff=sizeof(header);header.e_phentsize=sizeof(Elf64_Phdr);header.e_phnum=1;
    Elf64_Phdr load{};load.p_type=PT_LOAD;load.p_flags=PF_R|PF_W;load.p_filesz=load.p_memsz=4096;
    std::memcpy(bytes.data(),&header,sizeof(header));std::memcpy(bytes.data()+header.e_phoff,&load,sizeof(load));
    return bytes;
}
}
int main(int argc,char** argv) {
    assert(argc==4);const std::string scenario=argv[3];
    const bool init_failure=scenario=="init-fail" || scenario=="init-throw";
    const bool image_failure=scenario=="image-fail" || scenario=="image-throw";
    const bool simple=init_failure || image_failure || scenario=="unknown-image" || scenario=="version";
    std::vector<std::uint8_t> original=Synthetic();
    if (!simple) {
        const char* path=std::getenv("CACHYBORNE_TEST_ELF");if (!path) return 77;
        std::ifstream file(path,std::ios::binary);original.assign(std::istreambuf_iterator<char>(file),{});
    }
    assert(!bbgpu_plugin_hle_backend_install(Add,Retire));
    constexpr std::size_t capacity=512u<<20,code_offset=0x2034770;
    const auto page=std::size_t(sysconf(_SC_PAGESIZE));
    auto* memory=static_cast<unsigned char*>(mmap(nullptr,capacity,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
    auto* heap_memory=static_cast<std::uint32_t*>(mmap(nullptr,page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
    assert(memory!=MAP_FAILED && heap_memory!=MAP_FAILED);heap_memory[0]=987;
    Heap heap{reinterpret_cast<std::uintptr_t>(heap_memory)};
    std::size_t data_offset=0;
    if (!simple) {
        Elf64_Ehdr header;std::memcpy(&header,original.data(),sizeof(header));
        for (unsigned i=0;i<header.e_phnum;++i) {
            Elf64_Phdr load;std::memcpy(&load,original.data()+header.e_phoff+i*sizeof(load),sizeof(load));
            if (load.p_type==PT_LOAD && (load.p_flags&PF_W) && !(load.p_flags&PF_X) && load.p_memsz>page*4) {
                data_offset=(load.p_vaddr+page-1)&~(page-1);break;
            }
        }
        assert(data_offset);
        std::memset(memory+code_offset,0x90,14);
        const std::array<unsigned char,8> body{0x48,0x8d,0x47,0x05,0xc3,0x90,0x90,0x90}; // return arg0+5
        std::memcpy(memory+code_offset+14,body.data(),body.size());
        assert(!mprotect(memory+(code_offset&~(page-1)),page,PROT_READ|PROT_EXEC));
    }
    auto image=BbPluginImage::Image::Create(original,memory,capacity,Mapped,&heap);assert(image);
    const auto expected_sha=image->Sha256();
    BbPluginHost::Configuration configuration{.data_dir="native-test-data",.mods_dir="native-test-mods",.values={
        {"case",scenario},{"number","77"},{"expected_data","native-test-data"},{"expected_mods","native-test-mods"},
        {"expected_sha",image->Sha256()},{"code",std::to_string(0x400000+code_offset)},
        {"data",std::to_string(0x400000+data_offset)}}};
    std::string error;const char* cached_path=nullptr;
    {
        BbPluginHost::Host host(std::move(configuration));heap.host=&host;
        const auto& api=host.Api();assert(api.version==1 && api.size==offsetof(BbHostApi,on_frame) && !api.on_frame);
        assert(!std::strcmp(api.config("number"),"77")); // process/HLE callers need no callback scope
        bool duplicate_host_refused=false;
        try { BbPluginHost::Host duplicate({}); }
        catch(const std::logic_error&) { duplicate_host_refused=true; }
        assert(duplicate_host_refused);
        if (scenario=="version") {
            assert(!host.Load(argv[2],error) && !error.empty() && !hle);
        } else {
            assert(host.Load(argv[1],error)==!init_failure);
            assert(init_failure==!error.empty());
            void* handle=dlopen(argv[1],RTLD_NOW|RTLD_NOLOAD);assert(handle);
            auto path=reinterpret_cast<const char*(*)()>(dlsym(handle,"bb_fixture_path"));assert(path);
            auto count=reinterpret_cast<unsigned(*)()>(dlsym(handle,"bb_fixture_hook_calls"));assert(count);
            cached_path=path();assert(cached_path && !std::strcmp(cached_path,"native-test-data"));
            if (init_failure) assert(!hle);
            else {
                assert(hle && reinterpret_cast<std::int32_t(*)(std::uint32_t)>(hle)(7)==84);
                assert(!host.Load(argv[1],error) && !error.empty());
                // Worker/HLE queries may race the one-time image publication.
                std::jthread observer;
                if (!simple) observer=std::jthread([&](std::stop_token stop) {
                    do {
                        const char* sha=api.eboot_sha256();
                        assert(!*sha || expected_sha==sha);
                        const auto address=api.guest_addr(0x400000+code_offset);
                        assert(!address || address==reinterpret_cast<std::uintptr_t>(memory+code_offset));
                        std::this_thread::yield();
                    } while (!stop.stop_requested());
                });
                assert(host.Image(std::move(image),error)==!image_failure);
                if (observer.joinable()) { observer.request_stop();observer.join(); }
                if (image_failure) assert(!hle && !error.empty());
                else {
                    assert(!host.Image({},error) && !error.empty());
                    assert(!host.Load(argv[1],error) && !error.empty());
                    if (!simple) {
                        auto function=reinterpret_cast<std::uint64_t(*)(std::uint64_t)>(memory+code_offset);
                        assert(function(7)==12 && count()==1);
                        assert(function(7)==12 && count()==(scenario=="hook-throw"?1:2));
                        assert(scenario=="hook-throw"?!hle:bool(hle));
                        if (scenario=="service-close") {
                            heap.block=true;std::uint32_t value=0;
                            auto read=std::async(std::launch::async,[&] { return api.read(heap.address,&value,sizeof(value)); });
                            assert(heap.entered.try_acquire_for(5s));
                            auto close=std::async(std::launch::async,[&] { return host.Close(); });
                            assert(close.wait_for(100ms)==std::future_status::timeout);
                            heap.release.release();assert(read.get()==0 && value==987 && close.get());
                        } else if (scenario=="self-close") {
                            heap.self_close=true;std::uint32_t value=0;
                            assert(!api.read(heap.address,&value,sizeof(value)) && value==987 && !heap.inside_close);
                        }
                        assert(host.Close());
                        const auto calls=count();assert(function(7)==12 && count()==calls);
                    }
                }
            }
            assert(!dlclose(handle));
        }
        assert(host.Close() && !hle);
        assert(!*api.config("number") && !*api.data_dir() && !*api.eboot_sha256() && !api.eboot_is_109());
        std::uint32_t value=99;
        assert(api.read(0x400000,&value,sizeof(value))==1 && value==99);
        assert(!std::strcmp(cached_path?cached_path:"native-test-data","native-test-data"));
    }
    if (cached_path) assert(!std::strcmp(cached_path,"native-test-data")); // process-resident API strings
    // Test-owned execution is joined and its HLE backend has no active calls.
    // The real runtime keeps guest mappings for the lifetime of its process.
    assert(!munmap(memory,capacity) && !munmap(heap_memory,page));
}
