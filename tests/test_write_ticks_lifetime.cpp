// SPDX-License-Identifier: GPL-2.0-or-later
// A late process-exit consumer must still see the actual GPU write tables.
#include "buffers/buffer_cache_internal.h"
#include <cassert>
#include <cstdlib>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace {
constexpr u64 Address = 0x100007660;
void LateConsumer() {
    // Registered before the first lookup, so function-local destructors would
    // run before this callback. Use a real recording-side thread as at shutdown.
    std::thread reader([] {
        for (unsigned i = 0; i < 10000; ++i) {
            assert(VideoCore::NewestGpuWrite(Address, 16) == 73);
            assert(VideoCore::NewestGpuWrite(Address + 0x100000, 16) == 91);
        }
    });
    reader.join();
}
}

int main() {
    const pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        assert(std::atexit(LateConsumer) == 0);
        VideoCore::WriteTicks().Note(Address, 16, 73);
        VideoCore::PreciseTicks().Note(Address + 0x100000, 16, 91);
        assert(VideoCore::NewestGpuWrite(Address, 16) == 73);
        assert(VideoCore::NewestGpuWrite(Address + 0x100000, 16) == 91);
        std::exit(0);
    }
    int status = 0;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    std::puts("GPU write tables survive late exit consumers on recording threads");
}
