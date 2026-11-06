// SPDX-License-Identifier: GPL-2.0-or-later
// Complete retail rename and collector, with controlled allocator/lock imports.
// Recovery must never rerun a rename after its copy was already obtained.
#include "retail_timing_kit.h"
#include "games/bloodborne/gx_reclaim.h"
#include "shim/bbport_toggles.h"
#include <array>
#include <thread>

namespace {
unsigned attempts=0,copies=0,walks=0,progress_calls=0;
bool ready=false,table_available=false,ready_on_walk=false,ready_on_progress=false;
bool endless_progress=false,release_pool=false;
unsigned success_stage=0;
std::vector<unsigned> stages;
std::array<u64,2> copy_block{0,0x12345678};
std::array<u64,8> table_block{};
u64 pool=0,context=0;
u64 Copy(u64,u32 size,u32 alignment) {
    assert(size==64 && alignment==16);
    ++attempts;
    if (!ready) return 0;
    ++copies;
    return reinterpret_cast<u64>(copy_block.data());
}
u64 Table(u64,u64 size,u32 alignment) {
    assert(size==56 && alignment==8);
    return table_available ? reinterpret_cast<u64>(table_block.data()) : 0;
}
void Lock(u64 mutex,u32 timeout) {
    assert(mutex==pool+0xd8 && timeout==0xffffffff);
    ++walks;
    if (ready_on_walk) ready=true;
    if (release_pool && ready) store<u64>(pool+0xc8,2);
}
void Unlock(u64 mutex) { assert(mutex==pool+0xd8); }
u64 CompareExchange(u64 address,u64 value,u64 expected) {
    const u64 old=load<u64>(address);
    if (old==expected) store<u64>(address,value);
    return old;
}
u64 ResourceId(u64) { return 0xabcdef; }
bool Progress(unsigned stage,u64 timeout) {
    assert(stage<=2 && timeout>0 && timeout<=150000000);
    ++progress_calls;
    stages.push_back(stage);
    if (ready_on_progress && stage==success_stage) { ready=true; return true; }
    return endless_progress;
}
}

int main(int argc,char**) {
    if (!eboot_kit::load()) return 77;
    const bool off=argc>1;
    setenv("BB_GX_RENAME_WAIT",off ? "0" : "1",1);
    eboot_kit::stub(0x247ebc0,reinterpret_cast<void*>(&CompareExchange));
    eboot_kit::stub(0x256e1b0,reinterpret_cast<void*>(&ResourceId));
    auto rename=eboot_kit::fn<u32(*)(u64,u64,u64,u64)>(0x2ad0d30);
    std::array<u64,12> vtable{};
    vtable[3]=reinterpret_cast<u64>(&Copy);
    vtable[11]=reinterpret_cast<u64>(&Table);
    std::array<u64,6> mutex_vtable{};
    mutex_vtable[3]=reinterpret_cast<u64>(&Lock);
    mutex_vtable[5]=reinterpret_cast<u64>(&Unlock);
    std::vector<u8> pool_storage(0x100),context_storage(0xb4a0),resource(0x30),table(0x30);
    pool=reinterpret_cast<u64>(pool_storage.data());
    context=reinterpret_cast<u64>(context_storage.data());
    store<u64>(pool+0xd8,reinterpret_cast<u64>(mutex_vtable.data()));
    std::array<u64,2> allocator{reinterpret_cast<u64>(vtable.data()),pool};
    std::array<u64,2> indirect{0,reinterpret_cast<u64>(allocator.data())};
    const u64 res=reinterpret_cast<u64>(resource.data()),tab=reinterpret_cast<u64>(table.data());
    store<u64>(context+0xb490,reinterpret_cast<u64>(indirect.data()));
    store<u32>(res+0x14,64);
    const auto reset=[&] {
        attempts=copies=walks=progress_calls=0;
        ready=table_available=ready_on_walk=ready_on_progress=endless_progress=release_pool=false;
        success_stage=0;stages.clear();
        std::fill(table.begin(),table.end(),0);
        store<u64>(tab,reinterpret_cast<u64>(allocator.data()));
        store<u32>(tab+0x18,2);
        store<u64>(pool+0xc8,3);
        store<u64>(pool+0xd0,128);
    };
    u64 out=0xdeadbeef;
    reset();
    assert(rename(tab,context,reinterpret_cast<u64>(&out),res)==9);
    assert(attempts==2 && copies==0 && walks==1 && out==0xdeadbeef);
    auto* image=eboot_kit::image;
    assert(!BbGxReclaim::Install(nullptr,0,Progress));
    assert(!BbGxReclaim::Install(image,32,Progress));
    for (u64 address : {0x2ad0da9ull,0x2ad0d85ull,0x2aade22ull,0x2aaa98bull}) {
        auto* site=eboot_kit::at(address);site[0]^=1;
        assert(!BbGxReclaim::Install(image,0x6000000,Progress));
        assert(!BbGxReclaim::CollectorInstalled());
        site[0]^=1;
    }
    assert(BbGxReclaim::Install(image,0x6000000,Progress));
    assert(BbGxReclaim::Install(image,0x6000000,Progress));
    assert(!BbGxReclaim::Install(image+16,0x6000000,Progress));
    assert(BbGxReclaim::CollectorInstalled());
    reset();
    ready_on_progress=true;table_available=true;
    assert(rename(tab,context,reinterpret_cast<u64>(&out),res)==(off ? 9u : 0u));
    if (off) {
        assert(progress_calls==0 && attempts==2 && copies==0 && out==0xdeadbeef);
        assert(!BbGxReclaim::Read().waiting);
        std::puts("PASS: GX waits disabled preserve original failure and mapped-output bytes");
        return 0;
    }
    assert(copies==1 && attempts==3 && walks==2 && progress_calls==1 && out==copy_block[1]);
    // GPU step, recorded flush and decode+flush each can independently recover.
    for (unsigned stage=0;stage<3;++stage) {
        reset();ready_on_progress=true;success_stage=stage;table_available=true;
        assert(rename(tab,context,reinterpret_cast<u64>(&out),res)==0);
        assert(copies==1 && attempts==3 && stages.back()==stage);
    }
    // Recovered copy, table failure: exactly ONE copy consumed, no whole-rename retry.
    reset();ready_on_progress=true;
    out=0xdeadbeef;
    assert(rename(tab,context,reinterpret_cast<u64>(&out),res)==9);
    assert(copies==1 && attempts==3 && progress_calls==1 && out==0xdeadbeef);
    // The game's own reclaim recovers first: no host wait even if the table fails.
    reset();ready_on_walk=true;
    assert(rename(tab,context,reinterpret_cast<u64>(&out),res)==9);
    assert(copies==1 && attempts==2 && progress_calls==0 && out==0xdeadbeef);
    // Ordinary successful Maps never enter the copy slow-path observer.
    reset();ready=table_available=true;
    const auto slow_before=BbGxReclaim::Read().copy_slow_paths;
    assert(rename(tab,context,reinterpret_cast<u64>(&out),res)==0);
    assert(copies==1 && attempts==1 && walks==0 && progress_calls==0);
    assert(BbGxReclaim::Read().copy_slow_paths==slow_before);
    reset();endless_progress=true;
    out=0xdeadbeef;
    assert(rename(tab,context,reinterpret_cast<u64>(&out),res)==9);
    assert(attempts==68 && copies==0 && progress_calls==66 && out==0xdeadbeef);
    reset();
    assert(rename(tab,context,reinterpret_cast<u64>(&out),res)==9);
    assert(attempts==2 && progress_calls==3 && copies==0 && out==0xdeadbeef);
    // Exact retail return address on a host-created thread stack. The pool's
    // lock models a block becoming free once the progress callback completes.
    // The collector body itself remains retail and runs in full.
    const u8 caller[]={0x48,0x83,0xec,0x08,0xe8,0x39,0xca,0xff,0xff,0x48,0x83,0xc4,0x08,0xc3};
    auto* callsite=eboot_kit::at(0x2aade1e);
    assert(callsite[4]==0xe8); // retail call ending at 0x2aade27
    std::memcpy(callsite,caller,sizeof caller);
    auto call_collect=eboot_kit::fn<void(*)(u64,u64)>(0x2aade1e);
    reset();ready_on_progress=release_pool=true;
    assert(BbStats::enabled); // CTest enables BB_FRAME_STATS before static initialization
    u64 cb=context;
    std::thread host_thread([&] { call_collect(pool,reinterpret_cast<u64>(&cb)); });
    host_thread.join();
    assert(load<u64>(pool+0xc8)==2 && walks==2 && progress_calls==1);
    assert(BbStats::range_allocators[0].load()==pool && BbStats::range_live[0].load()==3);
    const auto stats=BbGxReclaim::Read();
    assert(stats.installed && stats.waiting && stats.recovered>=5 && stats.exhausted>=2);
    assert(stats.pool_waits==1 && stats.pool_recovered==1 && stats.steps>=64);
    std::puts("PASS: retail copy and collector hooks, three progress stages, table-failure safety, no normal-path hook, bounded retries, host-stack caller and shared statistics");
}
