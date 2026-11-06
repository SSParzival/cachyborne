// SPDX-License-Identifier: GPL-2.0-or-later
// Real DMA-BUF chunks and 16 MiB GPU reads: elapsed time is not a completion fence.
#include "shim/bbport_guest_memory_internal.h"
#include "buffers/buffer.h"
#include "buffers/guest_chunk_views.h"
#include "render/scheduler.h"
#include "vulkan/instance.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <thread>
#include <unistd.h>

extern "C" void runtime_test_set_discard_direct(void (*callback)(uint64_t,uint64_t));

namespace {
constexpr u64 Window = 16ull << 20;
std::array<std::atomic<unsigned>,2> discarded{};
std::array<std::atomic<bool>,2> views_released{};
std::atomic<bool> bad_discard{false};

void Discard(uint64_t phys, uint64_t size) {
    const auto index = phys / Window;
    if (size != Window || phys % Window || index >= discarded.size() || !views_released[index]) {
        bad_discard = true;
        return;
    }
    ++discarded[index];
}

void RecordRead(Vulkan::Scheduler& scheduler, const VideoCore::Buffer& source,
                const VideoCore::Buffer& destination, u32 pattern,
                const vk::detail::DispatchLoaderDynamic& dispatch) {
    const auto cmd = scheduler.CommandBuffer();
    cmd.fillBuffer(source.Handle(),0,Window,pattern,dispatch);
    const vk::MemoryBarrier2 to_read{
        .srcStageMask=vk::PipelineStageFlagBits2::eTransfer,
        .srcAccessMask=vk::AccessFlagBits2::eTransferWrite,
        .dstStageMask=vk::PipelineStageFlagBits2::eTransfer,
        .dstAccessMask=vk::AccessFlagBits2::eTransferRead,
    };
    cmd.pipelineBarrier2({.memoryBarrierCount=1,.pMemoryBarriers=&to_read},dispatch);
    const vk::BufferCopy copy{0,0,Window};
    cmd.copyBuffer(source.Handle(),destination.Handle(),copy,dispatch);
    const vk::MemoryBarrier2 to_host{
        .srcStageMask=vk::PipelineStageFlagBits2::eTransfer,
        .srcAccessMask=vk::AccessFlagBits2::eTransferWrite,
        .dstStageMask=vk::PipelineStageFlagBits2::eHost,
        .dstAccessMask=vk::AccessFlagBits2::eHostRead,
    };
    cmd.pipelineBarrier2({.memoryBarrierCount=1,.pMemoryBarriers=&to_host},dispatch);
}

bool CheckRead(VideoCore::Buffer& buffer, u32 pattern) {
    buffer.Invalidate(0,Window);
    const auto* words=reinterpret_cast<const u32*>(buffer.mapped_data.data());
    return std::all_of(words,words+Window/sizeof(u32),
                       [pattern](u32 word) { return word==pattern; });
}
}

int main() {
    setenv("BB_LAYER_MEMORY","1",1);
    setenv("BB_GUEST_MEMORY","dmabuf",1);
    setenv("BB_GUEST_VRAM","0",1);
    setenv("BB_GUEST_UNCACHED","0",1);
    setenv("BB_GUEST_LAZY","0",1);
    setenv("BB_TIMELINE_POLL_US","1000000",1);
    Vulkan::Instance instance(0,false);
    static vk::detail::DynamicLoader loader;
    vk::detail::DispatchLoaderDynamic dispatch;
    dispatch.init(loader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr"));
    dispatch.init(instance.GetInstance());
    dispatch.init(instance.GetDevice());
    if (!BbGuestMemory::MemoryModuleFits(instance) || !BbGuestMemory::Usable(instance)) return 77;
    Vulkan::Scheduler scheduler(instance);
    const auto device=instance.GetDevice();
    const vk::SemaphoreTypeCreateInfo type{.semaphoreType=vk::SemaphoreType::eTimeline};
    const auto gate=Vulkan::Check(device.createSemaphore({.pNext=&type},nullptr,dispatch));
    runtime_test_set_discard_direct(Discard);
    VideoCore::GuestChunkViews cache;
    std::array<VideoCore::Buffer*,2> views{};
    std::array<u32,2> indices{};
    for (u32 i=0;i<2;++i) {
        // Same slot in the former modulo-4096 cache, different live chunks.
        BbGuestMemory::detail::next_index=i*4096;
        const int fd=BbGuestMemory::detail::AllocChunk(i*Window,Window);
        assert(fd>=0);
        close(fd); // the Vulkan allocations, not these exported handles, keep the chunks alive
        const auto* chunk=BbGuestMemory::Find(i*Window);
        assert(chunk);
        indices[i]=chunk->index;
        views[i]=&cache.Get(instance,*chunk);
        assert(&cache.Get(instance,*chunk)==views[i]);
    }
    assert(indices[0]==0 && indices[1]==4096 && views[0]!=views[1]);
    assert(cache.Contains(views[0]) && cache.Contains(views[1]));
    VideoCore::Buffer output_a(instance,0,Window,VideoCore::MemoryType::HostCached);
    VideoCore::Buffer output_b(instance,0,Window,VideoCore::MemoryType::HostCached);
    RecordRead(scheduler,*views[0],output_a,0x5ca1ab1e,dispatch);
    Vulkan::SubmitInfo first;
    first.AddWait(gate,1);
    scheduler.Flush(first); // a real pending read, waiting on a private host-signaled semaphore
    BbGuestMemory::detail::FreeChunk(0,Window);
    std::this_thread::sleep_for(std::chrono::milliseconds(3100));
    const auto expired_a=BbGuestMemory::RetiredChunks();
    BbGuestMemory::DestroyRetiredChunks(scheduler,[view=cache.Take(indices[0])]() mutable {
        view.reset(); views_released[0]=true;
    });
    const u64 done_a=scheduler.CurrentTick();
    scheduler.Flush();

    // Another batch ages while the first callback is pending. It must remain
    // owned by its own later tick, not by the earlier callback's global queue.
    BbGuestMemory::detail::FreeChunk(Window,Window);
    std::this_thread::sleep_for(std::chrono::milliseconds(3100));
    const auto expired_b=BbGuestMemory::RetiredChunks();
    auto* second_view=views[1];
    const bool exact_take=!cache.Contains(views[0]) && cache.Contains(views[1]) &&
                          !cache.Take(indices[0]);
    BbGuestMemory::DestroyRetiredChunks(scheduler,[view=cache.Take(indices[1])]() mutable {
        view.reset(); views_released[1]=true;
    });
    scheduler.PopPendingOperations();
    const bool held_pending=!views_released[0] && !views_released[1] &&
                            discarded[0]==0 && discarded[1]==0;
    Vulkan::Check(device.signalSemaphore({.semaphore=gate,.value=1},dispatch));
    scheduler.Wait(done_a);
    scheduler.PopPendingOperations();
    // Avoid submitting an invalid buffer if a future regression frees batch B
    // early. The failure is still observable without asking the GPU to read it.
    const bool separated=views_released[0] && !views_released[1] &&
                         discarded[0]==1 && discarded[1]==0;
    const bool read_a=CheckRead(output_a,0x5ca1ab1e);
    bool read_b=false;
    if (separated) {
        RecordRead(scheduler,*second_view,output_b,0x1234abcd,dispatch);
        Vulkan::SubmitInfo second;
        second.AddWait(gate,2);
        const u64 done_b=scheduler.CurrentTick();
        scheduler.Flush(second);
        Vulkan::Check(device.signalSemaphore({.semaphore=gate,.value=2},dispatch));
        scheduler.Wait(done_b);
        scheduler.PopPendingOperations();
        read_b=CheckRead(output_b,0x1234abcd);
    }
    // No asserts while submissions are deliberately gated.
    scheduler.Finish();
    scheduler.PopPendingOperations();
    device.destroySemaphore(gate,nullptr,dispatch);
    assert(expired_a==std::vector<u32>{indices[0]});
    assert(expired_b==std::vector<u32>{indices[1]});
    assert(exact_take && held_pending && separated && read_a && read_b && !bad_discard);
    assert(views_released[1] && discarded[1]==1);
    assert(BbGuestMemory::RetiredChunks().empty());
    runtime_test_set_discard_direct(nullptr);
    std::puts("PASS: two 16 MiB GPU reads, expired grace interval, timeline retirement, view order, independent batches and distinct chunk indices 0/4096");
}
