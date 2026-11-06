// SPDX-License-Identifier: GPL-3.0-or-later
#include "bbport_plugin_hooks.h"
#include "bbport_plugin_module.h"
#include "bbgpu.h"
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fstream>
#include <iterator>
#include <semaphore>
#include <string>
#include <sys/mman.h>
#include <thread>
#include <vector>
namespace {
std::unique_ptr<BbPluginImage::Image> image;
std::unique_ptr<BbPluginHooks::Hooks> hooks;
std::string scenario,address;
std::atomic<unsigned> frames{0},exceptions{0};
std::uint64_t hle_owner=0;
void* hle_function=nullptr;
int AddHle(std::uint64_t owner,const char* name,void* function) {
    assert(owner && name && !std::strcmp(name,"sceKernelUsleep") && function);
    hle_owner=owner;hle_function=function;return 0;
}
void RetireHle(std::uint64_t owner) {
    if(owner==hle_owner) { hle_owner=0;hle_function=nullptr; }
}
std::binary_semaphore entered{0},release{0};
const char* Config(const char* key) { return std::strcmp(key,"target")?scenario.c_str():address.c_str(); }
void Log(const char* text,...) {
    if(!std::strcmp(text,"frame")) ++frames;
    else if(!std::strcmp(text,"exception destroyed")) ++exceptions;
    else if(!std::strcmp(text,"blocked")) { entered.release();release.acquire(); }
    else assert(false);
}
int Hook(std::uint64_t bn,const void* p,std::size_t n,BbHookFn fn,void* u) { return hooks->Hook(bn,p,n,fn,u); }
int Hook2(std::uint64_t bn,const void* p,std::size_t n,BbHookFn2 fn,void* u) { return hooks->Hook2(bn,p,n,fn,u); }
std::uint64_t Addr(std::uint64_t bn) { return image->GuestAddress(bn); }
bool Loaded(const char* path) { void* h=dlopen(path,RTLD_NOW|RTLD_NOLOAD);if(!h)return false;assert(!dlclose(h));return true; }
void FreeUser(const char* path) {
    void* h=dlopen(path,RTLD_NOW|RTLD_NOLOAD);assert(h);
    auto free=reinterpret_cast<void(*)()>(dlsym(h,"bb_fixture_release"));assert(free);free();assert(!dlclose(h));
}
extern "C" void plugin_sum_template();
extern "C" const unsigned char plugin_sum_template_end[];
asm(".text\n.globl plugin_sum_template\nplugin_sum_template:\n.rept 14\nnop\n.endr\n"
    "mov %rdi,%rax\nlea (%rax,%rsi,2),%rax\nlea (%rdx,%rdx,2),%rdx\nadd %rdx,%rax\n"
    "lea (%rax,%rcx,4),%rax\nlea (%r8,%r8,4),%r8\nadd %r8,%rax\n"
    "lea (%r9,%r9,2),%r9\nlea (%rax,%r9,2),%rax\nret\n"
    ".globl plugin_sum_template_end\nplugin_sum_template_end:\n");
}
int main(int argc,char** argv) {
    assert(argc==2);
    const char* elf=std::getenv("CACHYBORNE_TEST_ELF");if(!elf)return 77;
    std::ifstream file(elf,std::ios::binary);
    std::vector<std::uint8_t> original((std::istreambuf_iterator<char>(file)),{});
    constexpr std::size_t capacity=512u<<20;
    auto* memory=static_cast<unsigned char*>(mmap(nullptr,capacity,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
    assert(memory!=MAP_FAILED);
    image=BbPluginImage::Image::Create(original,memory,capacity);assert(image && image->Is109());
    hooks=std::make_unique<BbPluginHooks::Hooks>(*image);
    BbHostApi api{};api.version=8;api.size=offsetof(BbHostApi,on_option);
    api.config=Config;api.log=Log;api.guest_addr=Addr;api.on_frame=BbPluginModule::Module::OnFrame;
    api.hook=Hook;api.hook2=Hook2;
    api.register_hle=BbPluginModule::Module::RegisterHle;
    assert(!bbgpu_plugin_hle_backend_install(AddHle,RetireHle));
    // Limited test API only: no product claim that all API8 services exist.
    BbPluginCallbacks::Registry registry;std::string error;
    using Module=BbPluginModule::Module;
    using Fn=std::uint64_t(*)(std::uint64_t);
    using Six=std::uint64_t(*)(std::uint64_t,std::uint64_t,std::uint64_t,std::uint64_t,std::uint64_t,std::uint64_t);
    using Float=double(*)(std::uint64_t,double,double);
    unsigned page=1;
    auto prepare=[&](const char* mode) {
        scenario=mode;const auto offset=std::size_t(page++)*4096;
        auto* code=memory+offset;
        if(scenario=="sum") {
            const auto start=reinterpret_cast<std::uintptr_t>(&plugin_sum_template);
            const auto size=reinterpret_cast<std::uintptr_t>(plugin_sum_template_end)-start;
            assert(size<128);std::memcpy(code,reinterpret_cast<void*>(start),size);
        } else if(scenario=="float") {
            std::memset(code,0x90,14);constexpr unsigned char body[]{0xf2,0x0f,0x5c,0xc1,0xc3};
            std::memcpy(code+14,body,sizeof(body));
        } else {
            constexpr unsigned char entry[]{0x48,0x8d,0x47,0x01};std::memcpy(code,entry,4);
            std::memset(code+4,0x90,10);code[14]=0xc3;
        }
        assert(!mprotect(code,4096,PROT_READ|PROT_EXEC));
        address=std::to_string(0x400000+offset);
        return reinterpret_cast<Fn>(code);
    };
    auto denied=prepare("integer");
    assert(Hook2(std::stoull(address),nullptr,14,nullptr,nullptr)==1);
    auto short_lived=Module::Open(argv[1],registry,api,error,BbPluginModule::Residency::CallbacksOnly);
    assert(short_lived && !short_lived->Image(error));short_lived.reset();
    assert(!Loaded(argv[1]) && denied(3)==4);

    auto legacy=prepare("legacy");auto module=Module::Open(argv[1],registry,api,error);assert(module && module->Image(error));
    auto six=reinterpret_cast<Six>(reinterpret_cast<std::uintptr_t>(legacy));
    assert(six(7,2,3,4,5,6)==8 && six(3,2,3,4,5,6)==0);
    assert(!registry.Dispatch() && frames==1);
    assert(module->Close());FreeUser(argv[1]);
    assert(legacy(3)==4 && !registry.Dispatch() && frames==1);

    auto run=prepare("guards");module=Module::Open(argv[1],registry,api,error);assert(module && module->Image(error));
    assert(run(2)==13 && run(3)==1003 && run(999)==1999);
    std::array<std::thread,8> workers;
    for(unsigned t=0;t<workers.size();++t) workers[t]=std::thread([&,t] {
        for(unsigned i=0;i<2000;++i) { const auto n=std::uint64_t(t+2);assert(run(n)==(n&1?n+1000:n+11)); }
    });
    for(auto& w:workers)w.join();
    std::thread blocked([&] { assert(run(777)==1777); });entered.acquire();
    std::atomic<bool> drained{false};std::thread retiring([&] { assert(module->Close());drained=true; });
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(run(3)!=4) { assert(std::chrono::steady_clock::now()<deadline);std::this_thread::yield(); }
    assert(!drained);release.release();blocked.join();retiring.join();
    FreeUser(argv[1]);assert(drained && run(3)==4 && run(999)==1000);

    auto sum=prepare("sum");module=Module::Open(argv[1],registry,api,error);assert(module && module->Image(error));
    assert(reinterpret_cast<Six>(reinterpret_cast<std::uintptr_t>(sum))(1,2,3,4,5,6)==441);
    assert(module->Close());FreeUser(argv[1]);
    assert(reinterpret_cast<Six>(reinterpret_cast<std::uintptr_t>(sum))(1,2,3,4,5,6)==91);

    auto floating=prepare("float");module=Module::Open(argv[1],registry,api,error);assert(module && module->Image(error));
    auto f=reinterpret_cast<Float>(reinterpret_cast<std::uintptr_t>(floating));
    assert(f(0,1.25,2.5)==-5. && f(1,1.25,2.5)==21.25);
    assert(module->Close());FreeUser(argv[1]);assert(f(0,1.25,2.5)==-1.25);

    auto throwing=prepare("integer");module=Module::Open(argv[1],registry,api,error);assert(module && module->Image(error));
    assert(throwing(888)==889 && exceptions==1); // partial context change was discarded
    assert(throwing(3)==4); // quarantine blocks every later callback
    assert(module->Close());FreeUser(argv[1]);

    auto resources=prepare("hle-throw");module=Module::Open(argv[1],registry,api,error);
    assert(module && hle_owner && hle_function && module->Image(error));
    assert(reinterpret_cast<int32_t(*)(uint32_t)>(hle_function)(7)==507);
    assert(resources(888)==889 && exceptions==2);
    assert(!hle_owner && !hle_function); // hook quarantine cleans HLE before Close
    assert(resources(3)==4 && module->Close());FreeUser(argv[1]);

    auto failed=prepare("image-fail");module=Module::Open(argv[1],registry,api,error);assert(module && !module->Image(error));
    FreeUser(argv[1]);assert(failed(3)==4 && !registry.Dispatch());
    assert(!munmap(memory,capacity));
}
