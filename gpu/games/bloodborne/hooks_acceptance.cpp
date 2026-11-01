// SPDX-License-Identifier: GPL-2.0-or-later
// Opt-in offline acceptance instrumentation. Calls run on the game's update
// thread; samples are drained to disk by a bounded E-core writer.
#include "hooks_internal.h"
#include "shim/bb_env.h"
#include "common/thread.h"
#include <fstream>
#include <thread>

namespace BbGuestHooks {
namespace {
template <typename T> T Read(u64 address) {
    T value{};
    if (address < 0x10000 || address > UINTPTR_MAX - sizeof(T)) return value;
    iovec local{&value, sizeof(T)}, remote{reinterpret_cast<void*>(address), sizeof(T)};
    if (process_vm_readv(getpid(), &local, 1, &remote, 1, 0) != ssize_t(sizeof(T))) return {};
    return value;
}
struct Pose {
    u64 ns{}, sequence{};
    u32 map{};
    bool loading{}, valid{};
    float x{}, y{}, z{}, heading{};
};
struct Capture {
    static constexpr u64 Capacity = 8192;
    std::array<Pose, Capacity> queue{};
    std::atomic<u64> head{0}, tail{0}, dropped{0};
    std::atomic<pid_t> owner{0};
    std::jthread writer;
    FILE* output = nullptr;
    const char* commands = nullptr;
    u64 sequence = 0, sampled_ns = 0, command_ns = 0;
    ~Capture() {
        if (writer.joinable()) { writer.request_stop(); writer.join(); }
        if (output) std::fclose(output);
    }
    void Start(FILE* file) {
        output = file;
        if (!file) return;
        std::fputs("ns,sequence,map,loading,valid,x,y,z,heading,dropped\n", file);
        writer = std::jthread([this](std::stop_token stop) {
            Common::SetCurrentThreadName("bb:AssetWorker");
            do {
                u64 read = tail.load(std::memory_order_relaxed);
                const u64 end = head.load(std::memory_order_acquire);
                for (; read != end; ++read) {
                    const auto& p = queue[read % Capacity];
                    std::fprintf(output, "%llu,%llu,%u,%d,%d,%.9g,%.9g,%.9g,%.9g,%llu\n",
                        (unsigned long long)p.ns, (unsigned long long)p.sequence, p.map,
                        p.loading, p.valid, p.x, p.y, p.z, p.heading,
                        (unsigned long long)dropped.load(std::memory_order_relaxed));
                }
                tail.store(read, std::memory_order_release);
                std::fflush(output);
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            } while (!stop.stop_requested() || tail.load() != head.load());
        });
    }
    void Push(const Pose& pose) {
        if (!output) return;
        const u64 next = head.load(std::memory_order_relaxed);
        if (next - tail.load(std::memory_order_acquire) >= Capacity) {
            dropped.fetch_add(1, std::memory_order_relaxed); return;
        }
        queue[next % Capacity] = pose;
        head.store(next + 1, std::memory_order_release);
    }
};
Capture capture;
struct LampRow { u64 vtable; u32 id; const u16* row; };
const u16* Lamp(u32 id) {
    LampRow result{ImageBase + 0x5320b40, ~0u, nullptr};
    reinterpret_cast<void (PS4_SYSV_ABI *)(LampRow*, u32)>(ImageBase + 0x1f26b40)(&result, id);
    return result.row;
}
u32 Map() {
    const u64 list = Read<u64>(ImageBase + 0x553b148);
    const int index = Read<int>(list + 0x20);
    const u64 array = Read<u64>(list + 0x10);
    const int count = Read<int>(array + 0x18);
    const u64 entries = Read<u64>(array + 0x20);
    return index >= 0 && index < count && count <= 1024 ? Read<u32>(entries + u64(index) * 0xa0 + 8) : 0;
}
void PS4_SYSV_ABI AcceptanceTick() {
    // The entry can also be reached by service work. Only its first update
    // thread owns the SPSC producer and command dispatch.
    const pid_t tid = gettid();
    pid_t zero = 0;
    capture.owner.compare_exchange_strong(zero, tid);
    if (capture.owner.load() != tid) return;
    const u64 now = u64(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    if (now - capture.sampled_ns < 2'000'000) return;
    capture.sampled_ns = now;
    Pose p{.ns = now, .sequence = capture.sequence, .map = Map(),
           .loading = Read<u8>(ImageBase + 0x556286b) != 0};
    const u64 world = Read<u64>(ImageBase + 0x553e878);
    const u64 player = Read<u64>(world + 0x60);
    const u64 character = Read<u64>(player + 0x58);
    const u64 state = Read<u64>(character + 8);
    const u64 model = Read<u64>(state + 0x3b0);
    const u64 transform = Read<u64>(model + 0x68);
    if (transform) {
        std::array<float, 8> values{};
        iovec local{values.data(), sizeof(values)}, remote{reinterpret_cast<void*>(transform + 0x1d0), sizeof(values)};
        const bool read = process_vm_readv(getpid(), &local, 1, &remote, 1, 0) == ssize_t(sizeof(values));
        p.x = values[4]; p.y = values[5]; p.z = values[6]; p.heading = values[1];
        p.valid = read && p.map && std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
                  std::isfinite(p.heading);
    }
    capture.Push(p);
    if (!capture.commands || now - capture.command_ns < 100'000'000 || p.loading || !p.valid) return;
    capture.command_ns = now;
    std::ifstream file(capture.commands);
    u64 seq = 0; std::string action; u32 lamp = 0;
    if (!(file >> seq >> action) || seq <= capture.sequence) return;
    if (action == "list") {
        capture.sequence = seq;
        for (u32 area = 20; area < 70; ++area) for (u32 block = 0; block < 10; ++block)
            for (u32 number = 950; number < 980; ++number) {
                const u32 id = area * 100000 + block * 10000 + 2000 + number;
                if (const auto* row = Lamp(id)) {
                    const u32 map = Read<u32>(reinterpret_cast<u64>(row));
                    std::printf("Acceptance lamp: %u map %u\n", id, map);
                }
            }
        std::printf("Acceptance command: %llu listed\n", (unsigned long long)seq);
    } else if (action == "warp" && (file >> lamp)) {
        capture.sequence = seq;
        if (lamp < 2000000 || lamp >= 7000000 || !Lamp(lamp)) {
            std::printf("Acceptance command: %llu rejected lamp %u\n", (unsigned long long)seq, lamp);
            return;
        }
        std::printf("Acceptance command: %llu warp %u from map %u\n", (unsigned long long)seq, lamp, p.map);
        std::fflush(stdout);
        reinterpret_cast<void (PS4_SYSV_ABI *)(u32)>(ImageBase + 0x13cdf30)(lamp);
    }
}
}
bool InstallAcceptance() {
    const char* poses = bb_env("BB_ACCEPTANCE_POSES");
    const char* commands = bb_env("BB_ACCEPTANCE_COMMAND");
    if ((!poses && !commands) || bb_env_on("BB_ONLINE") || bb_env_on("BB_BBSC")) return false;
    constexpr u64 site = 0x2034770;
    constexpr std::array<u8, 6> expected{0x55,0x48,0x89,0xe5,0x41,0x57};
    constexpr std::array<u8, 15> warp{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,
        0x53,0x48,0x83,0xec,0x38,0x89,0xfb};
    constexpr std::array<u8, 14> lookup{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,
        0x41,0x54,0x53,0x41,0x89,0xf4};
    if (std::memcmp(reinterpret_cast<void*>(ImageBase + site), expected.data(), expected.size()) ||
        std::memcmp(reinterpret_cast<void*>(ImageBase + 0x13cdf30), warp.data(), warp.size()) ||
        std::memcmp(reinterpret_cast<void*>(ImageBase + 0x1f26b40), lookup.data(), lookup.size())) return false;
    FILE* file = poses ? std::fopen(poses, "wx") : nullptr;
    if (poses && !file) return false;
    if (!Core::InstallGuestCodeHook(reinterpret_cast<void*>(ImageBase + site), expected, site,
        [](u64, const Core::GuestRegisterSnapshot*) { AcceptanceTick(); })) {
        if (file) std::fclose(file);
        return false;
    }
    capture.commands = commands;
    capture.Start(file);
    std::puts("Acceptance: offline update sampling and validated lamp commands enabled");
    return true;
}
} // namespace BbGuestHooks
