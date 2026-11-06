// SPDX-License-Identifier: GPL-2.0-or-later
// Real deferred scheduler and GPU: a command's first data copy must not be
// recycled while its second copy and command wait on the recorder thread.
#include "render/scheduler.h"
#include "vulkan/instance.h"
#include <vk_mem_alloc.h>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

int main() {
    setenv("BB_VK_RECORD_THREAD", "1", 1);
    setenv("BB_VK_RECORD_THREADS", "1", 1);
    setenv("BB_ASYNC_SUBMIT", "0", 1);
    Vulkan::Instance instance(0, false);
    static vk::detail::DynamicLoader loader;
    vk::detail::DispatchLoaderDynamic dispatch;
    dispatch.init(loader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr"));
    dispatch.init(instance.GetInstance());
    dispatch.init(instance.GetDevice());
    Vulkan::Scheduler scheduler(instance, true);
    assert(scheduler.IsRecordingDeferred());
    constexpr size_t bytes = 64 * 1024;
    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = 2 * bytes;
    info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VmaAllocationCreateInfo allocation{};
    allocation.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
    allocation.usage = VMA_MEMORY_USAGE_AUTO;
    allocation.requiredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    VkBuffer buffer{};
    VmaAllocation memory{};
    VmaAllocationInfo result{};
    assert(vmaCreateBuffer(instance.GetAllocator(), &info, &allocation, &buffer, &memory, &result) == VK_SUCCESS);
    std::memset(result.pMappedData, 0, 2 * bytes);
    std::vector<u32> a(bytes / 4, 0x12345678), b(bytes / 4, 0x9abcdef0);
    const auto first = scheduler.RecordData(std::span<const u32>(a));
    // Even an intervening explicit reservation cannot retire a returned span
    // before the command that consumes it has been stored.
    scheduler.ReserveRecordData(Vulkan::RecordChunk::Capacity + 1);
    const auto second = scheduler.RecordData(std::span<const u32>(b));
    std::atomic<bool> entered{false}, release{false};
    scheduler.Record([&, first, second](vk::CommandBuffer cmd) {
        entered.store(true, std::memory_order_release);
        entered.notify_one();
        while (!release.load(std::memory_order_acquire)) release.wait(false);
        cmd.updateBuffer(buffer, 0, first.size_bytes(), first.data(), dispatch);
        cmd.updateBuffer(buffer, bytes, second.size_bytes(), second.data(), dispatch);
    });
    scheduler.KickRecording(true);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!entered.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    assert(entered.load(std::memory_order_acquire));
    // Previously, RecordData(second) retired the first copy's data-only chunk.
    // The worker has now recycled it; this reservation takes and overwrites it
    // while the command still waits. With chunk ownership it stays unavailable.
    scheduler.ReserveRecordData(Vulkan::RecordChunk::Capacity + 1);
    std::vector<u32> overwrite(bytes / 4, 0xeeeeeeee);
    const auto scratch = scheduler.RecordData(std::span<const u32>(overwrite));
    assert(scratch.data() != first.data() && scratch.data() != second.data());
    std::fill(a.begin(), a.end(), 0);
    std::fill(b.begin(), b.end(), 0);
    release.store(true, std::memory_order_release);
    release.notify_one();
    scheduler.Record([&dispatch](vk::CommandBuffer cmd) {
        const vk::MemoryBarrier2 barrier{
            .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
            .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eHost,
            .dstAccessMask = vk::AccessFlagBits2::eHostRead};
        cmd.pipelineBarrier2({.memoryBarrierCount = 1, .pMemoryBarriers = &barrier}, dispatch);
    });
    scheduler.Finish();
    const auto* data = static_cast<const u32*>(result.pMappedData);
    for (size_t i = 0; i < bytes / 4; ++i) {
        assert(data[i] == 0x12345678 && data[i + bytes / 4] == 0x9abcdef0);
    }
    vmaDestroyBuffer(instance.GetAllocator(), buffer, memory);
    std::puts("PASS: deferred multi-copy lifetime through recording, attempted recycle and 128 KiB GPU readback");
}
