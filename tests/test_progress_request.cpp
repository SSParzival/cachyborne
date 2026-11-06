// SPDX-License-Identifier: GPL-2.0-or-later
#include "render/progress_request.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <memory>
#include <semaphore>
#include <thread>

int main() {
    using Request=Vulkan::GpuProgressRequest;
    using Clock=std::chrono::steady_clock;
    // A queued request cancelled on timeout never starts recording later.
    {
        auto request=std::make_shared<Request>();
        assert(!request->WaitUntil(Clock::now()));
        assert(!request->Begin());
    }
    // A completed tick of zero is distinct from no completion.
    {
        Request request;
        assert(request.Begin());
        request.Complete(0);
        const auto result=request.WaitUntil(Clock::now());
        assert(result && *result==0);
    }
    // An active producer outlives a timed-out waiter's stack/ownership, and
    // completion releases the last owner without accessing destroyed state.
    {
        auto request=std::make_shared<Request>();
        std::weak_ptr<Request> weak=request;
        std::binary_semaphore begun(0),release(0);
        std::thread producer([owned=request,&begun,&release] {
            assert(owned->Begin());
            begun.release();
            release.acquire();
            owned->Complete(42);
        });
        begun.acquire();
        assert(!request->WaitUntil(Clock::now()));
        request.reset();
        assert(!weak.expired());
        release.release();
        producer.join();
        assert(weak.expired());
    }
    // Completion during a wait carries the producer's exact tick.
    {
        auto request=std::make_shared<Request>();
        std::thread producer([request] {
            assert(request->Begin());
            request->Complete(9001);
        });
        const auto result=request->WaitUntil(Clock::now()+std::chrono::seconds(1));
        producer.join();
        assert(result && *result==9001);
    }
    std::puts("PASS: queued cancellation, zero tick, active timeout lifetime and asynchronous completion");
}
