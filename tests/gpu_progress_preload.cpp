// SPDX-License-Identifier: GPL-2.0-or-later
// Optional real-game integration harness, never loaded by normal launches.
// Build target gpu-progress-preload; prepend env LD_PRELOAD=<module> to ONLY
// the isolated game's command (inside gamescope), with BB_GPU_LABELS=0.
// capture_physics copies saves. After 30 s of valid world ticks, exercise the
// real renderer bridge on the guest pacer thread, once per stage, and log it.
#include "bbgpu.h"
#include "platform/gnm/gnm.h"
#include "shim/bbport_frame_tasks.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <sys/uio.h>
#include <unistd.h>

namespace {
unsigned char* image=nullptr;
unsigned ticks=0,stage=0;
template<class T> bool Read(std::uint64_t address,T& value) {
    iovec local{&value,sizeof value},remote{reinterpret_cast<void*>(address),sizeof value};
    return process_vm_readv(getpid(),&local,1,&remote,1,0)==static_cast<ssize_t>(sizeof value);
}
void Tick() {
    if (stage>=3) return;
    std::uint64_t manager=0,player=0;
    std::uint32_t block=0xffffffff;
    if (!Read(reinterpret_cast<std::uint64_t>(image)+0x553e878,manager) || !manager ||
        !Read(manager+0x60,player) || !player || !Read(player+0x3f8,block) || block==0xffffffff) return;
    if (++ticks<1800) return;
    const std::uint64_t budget=stage==2 ? 150000000 : 50000000;
    const auto start=std::chrono::steady_clock::now();
    const bool result=BbGnm::AdvanceGpuFor(stage,budget);
    const auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now()-start).count();
    // Stage 0 legitimately has no outstanding tick. Flush stages must complete.
    const bool pass=(stage==0 || result) && elapsed<1000000000;
    std::printf("GPU progress integration: stage=%u result=%d budget_ns=%llu elapsed_ns=%lld %s\n",
                stage,int(result),static_cast<unsigned long long>(budget),
                static_cast<long long>(elapsed),pass ? "PASS" : "FAIL");
    std::fflush(stdout);
    ++stage;
}
}

extern "C" void bbgpu_patch_image(unsigned char* base,std::uint64_t size) {
    using Patch=void(*)(unsigned char*,std::uint64_t);
    const auto original=reinterpret_cast<Patch>(dlsym(RTLD_NEXT,"bbgpu_patch_image"));
    if (!original) std::abort();
    original(base,size);
    image=base;
    if (!BbFrameTasks::Subscribe(base,size,&Tick)) std::abort();
    std::puts("GPU progress integration: armed; requires copied saves and CPU-label mode");
}
