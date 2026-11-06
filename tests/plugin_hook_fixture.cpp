// SPDX-License-Identifier: GPL-3.0-or-later
#include "plugins/bbhost_plugin.h"
#include <array>
#include <bit>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
namespace {
const BbHostApi* api;
std::uint64_t target;
int* state=nullptr;
int32_t BB_GUEST_ABI Replacement(uint32_t value) { return 500+static_cast<int32_t>(value); }
enum Mode { Integer,Legacy,Sum,Float } mode;
void Frame(void* user) { assert(*static_cast<int*>(user)==42);api->log("frame"); }
int First(const BbHookFrame* f,void* user) {
    assert(*static_cast<int*>(user)==42);
    if (f->rdi==7) assert(f->rsi==2 && f->rdx==3 && f->rcx==4 && f->r8==5 && f->r9==6);
    assert(!api->on_frame(Frame,user)); // hook context must carry module ownership
    constexpr unsigned char entry[14]={0x48,0x8d,0x47,0x01,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90};
    assert(api->hook(target,entry,sizeof(entry),First,user)==1); // no runtime installation
    return f->rdi==3;
}
struct Error : std::runtime_error {
    Error() : std::runtime_error("hook fixture") {}
    ~Error() override { api->log("exception destroyed"); }
};
int Second(BbHookCtx* c,void* user) {
    assert(*static_cast<int*>(user)==42);
    if (mode==Sum) { for(unsigned i=0;i<6;++i) c->arg[i]*=i+1;return 0; }
    if (mode==Float) {
        c->xmm[0]=std::bit_cast<std::uint64_t>(std::bit_cast<double>(c->xmm[0])*2.);
        c->xmm[1]=std::bit_cast<std::uint64_t>(std::bit_cast<double>(c->xmm[1])*3.);
        if(c->arg[0]) { c->xmm[0]=bb_f64(21.25);c->ret=991;return 1; }
        return 0;
    }
    const auto value=c->arg[0];
    if (value==777) api->log("blocked");
    if (value==888) { c->arg[0]=123;throw Error(); }
    if (value==999) {
        auto run=reinterpret_cast<std::uint64_t(*)(std::uint64_t)>(api->guest_addr(target));
        assert(run(2)==13 && run(3)==1003);c->ret=1999;return 1;
    }
    if (value&1) { c->ret=value+1000;return 1; }
    c->arg[0]+=10;return 0;
}
}
extern "C" {
BB_PLUGIN_EXPORT extern const BbPluginInfo bb_plugin_info={8,0,"hook-test","Hook test","1","Cachyborne","Test fixture"};
BB_PLUGIN_EXPORT int bb_plugin_init(const BbHostApi* a) {
    api=a;target=std::strtoull(api->config("target"),nullptr,0);
    if(!std::strcmp(api->config("case"),"hle-throw"))
        assert(!api->register_hle("sceKernelUsleep",reinterpret_cast<void*>(Replacement)));
    constexpr unsigned char entry[14]={0x48,0x8d,0x47,0x01,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90};
    assert(api->hook(target,entry,sizeof(entry),First,nullptr)==1);return 0;
}
BB_PLUGIN_EXPORT int bb_plugin_image(const BbHostApi*) {
    const char* test=api->config("case");
    mode=!std::strcmp(test,"legacy")?Legacy:!std::strcmp(test,"sum")?Sum:!std::strcmp(test,"float")?Float:Integer;
    state=new int(42);
    constexpr unsigned char entry[14]={0x48,0x8d,0x47,0x01,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90};
    if (!std::strcmp(test,"guards")) {
        auto bad=entry[0]^1;
        auto copy=std::array<unsigned char,14>{};std::memcpy(copy.data(),entry,14);copy[0]=bad;
        assert(api->hook(target,nullptr,14,First,state)==1);
        assert(api->hook(target,entry,13,First,state)==1);
        assert(api->hook(target,entry,65,First,state)==1);
        assert(api->hook(target,copy.data(),14,First,state)==1);
        assert(api->hook(target,reinterpret_cast<const void*>(1),14,First,state)==1);
        assert(api->hook2(target,nullptr,14,nullptr,state)==1);
        assert(api->hook2(UINT64_MAX,nullptr,14,Second,state)==1);
    }
    int result=mode==Legacy?api->hook(target,entry,sizeof(entry),First,state):api->hook2(target,nullptr,14,Second,state);
    if (result) { delete state;state=nullptr;return result; }
    assert(api->hook2(target,nullptr,14,Second,state)==2); // overlap cannot replace an installed hook
    return !std::strcmp(test,"image-fail")?7:0;
}
BB_PLUGIN_EXPORT void bb_fixture_release(void) { delete state;state=nullptr; }
}
