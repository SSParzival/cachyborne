// SPDX-License-Identifier: GPL-3.0-or-later
#include "bbport_plugin_callbacks.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <future>
#include <semaphore>
#include <stdexcept>
#include <thread>
using Registry=BbPluginCallbacks::Registry;
namespace {
void Count(void* p) { ++*static_cast<std::atomic<unsigned>*>(p); }
void Throw(void*) { throw std::runtime_error("quarantine"); }
struct Action {
    Registry* registry;
    Registry::Owner* self;
    std::atomic<unsigned> calls{0};
    std::atomic<bool> done{false};
    std::binary_semaphore entered{0},release{0};
    bool block=false;
    Registry* other_registry=nullptr;
    Registry::Owner* other=nullptr;
};
void Disable(void* p) noexcept {
    auto& a=*static_cast<Action*>(p);++a.calls;
    // Reentry demonstrates that the action runs outside all registry locks.
    std::atomic<unsigned> unused{0};
    assert(!a.registry->Subscribe(*a.self,Count,&unused));
    assert(!a.registry->Retire(*a.self)); // no self-wait on this action
    if (a.other) assert(!a.other_registry->Retire(*a.other));
    a.entered.release();if(a.block)a.release.acquire();
    a.done=true;
}
}
int main() {
    Registry r,other;Registry::Owner owner;
    auto action=std::make_shared<Action>();action->registry=&r;action->self=&owner;
    owner=r.CreateOwner(std::make_shared<int>(1),{Disable,action});
    assert(r.Retire(owner) && action->calls==1 && action->done);
    assert(r.Retire(owner) && action->calls==1);
    action=std::make_shared<Action>();action->registry=&r;action->self=&owner;
    owner=r.CreateOwner(std::make_shared<int>(2),{Disable,action});
    assert(r.Subscribe(owner,Throw,nullptr));
    assert(r.Dispatch()==1 && action->calls==1 && action->done);
    assert(!r.Dispatch() && r.Retire(owner) && action->calls==1);

    action=std::make_shared<Action>();action->registry=&r;action->self=&owner;action->block=true;
    owner=r.CreateOwner(std::make_shared<int>(3),{Disable,action});
    auto first=std::async(std::launch::async,[&] { assert(r.Retire(owner));assert(action->done); });
    action->entered.acquire();
    Registry::Invocation denied{owner};assert(!denied);
    std::binary_semaphore started{0};
    auto second=std::async(std::launch::async,[&] {
        started.release();assert(r.Retire(owner));assert(action->done);
    });
    started.acquire();
    assert(first.wait_for(std::chrono::milliseconds(20))==std::future_status::timeout);
    assert(second.wait_for(std::chrono::milliseconds(20))==std::future_status::timeout);
    action->release.release();first.get();second.get();
    assert(action->calls==1);

    // Mutual resource retirement must not wait on the outer disable action.
    Registry::Owner a,b;
    auto ca=std::make_shared<Action>();ca->registry=&r;ca->self=&a;ca->other_registry=&other;ca->other=&b;
    auto cb=std::make_shared<Action>();cb->registry=&other;cb->self=&b;cb->other_registry=&r;cb->other=&a;
    a=r.CreateOwner(std::make_shared<int>(4),{Disable,ca});
    b=other.CreateOwner(std::make_shared<int>(5),{Disable,cb});
    assert(r.Retire(a) && other.Retire(b));
    assert(ca->calls==1 && cb->calls==1 && ca->done && cb->done);
}
