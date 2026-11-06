// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_plugin_callbacks.h"
#include <array>
#include <atomic>
#include <cassert>
#include <dlfcn.h>
#include <semaphore>
#include <stdexcept>
#include <thread>

using BbPluginCallbacks::Registry;
namespace {
void Count(void* p) { ++*static_cast<std::atomic<unsigned>*>(p); }
struct Retiring {
    Registry* registry;
    Registry::Owner* owner;
    bool called = false;
};
void RetireInside(void* p) {
    auto& r=*static_cast<Retiring*>(p);
    r.called=true;
    assert(!r.registry->Retire(*r.owner));
}
void Throw(void*) { throw std::runtime_error("plugin failure"); }
struct Blocked {
    std::binary_semaphore entered{0}, release{0};
    std::atomic<unsigned> calls{0};
};
void Block(void* p) {
    auto& b=*static_cast<Blocked*>(p);
    ++b.calls;b.entered.release();b.release.acquire();
}
struct CallbackFixture { void (*host)(void*); void* context; };
}
int main(int argc,char** argv) {
    assert(argc==2);
    Registry r,other;
    Registry::Owner empty;
    std::atomic<unsigned> calls{0};
    assert(!r.Subscribe(r.CreateOwner({}),Count,&calls));
    assert(!r.Subscribe(empty,Count,&calls) && !r.Retire(empty));
    auto a=r.CreateOwner(std::make_shared<int>(1));
    assert(!other.Subscribe(a,Count,&calls) && !other.Retire(a));
    assert(!r.Subscribe(a,nullptr,&calls));
    assert(r.Subscribe(a,Count,&calls) && r.Subscribe(a,Count,&calls));
    assert(!Registry::SubscribeCurrent(Count,&calls));
    {
        Registry::Scope scope{r,a};
        assert(Registry::SubscribeCurrent(Count,&calls)); // same entry, dedup
        {
            Registry::Scope invalid{other,a};
            assert(!Registry::SubscribeCurrent(Count,&calls));
        }
        assert(Registry::SubscribeCurrent(Count,&calls));
        std::thread unrelated([&] { assert(!Registry::SubscribeCurrent(Count,&calls)); });
        unrelated.join();
    }
    assert(!Registry::SubscribeCurrent(Count,&calls));
    assert(!r.Dispatch() && calls==1);
    auto b=r.CreateOwner(std::make_shared<int>(2));
    assert(r.Subscribe(b,Count,&calls));
    assert(!r.Dispatch() && calls==3); // same function/user, different owners
    assert(r.Retire(a) && r.Retire(a));
    assert(!r.Subscribe(a,Count,&calls));
    assert(!r.Dispatch() && calls==4);
    assert(r.Retire(b));

    // Removal during a snapshot skips later callbacks of the same owner.
    a=r.CreateOwner(std::make_shared<int>(3));
    Retiring self{&r,&a};
    assert(r.Subscribe(a,RetireInside,&self));
    assert(r.Subscribe(a,Count,&calls));
    assert(!r.Dispatch() && self.called && calls==4);
    assert(r.Retire(a));
    // Cross-registry retirement also refuses to wait inside a callback.
    a=r.CreateOwner(std::make_shared<int>(4));
    b=other.CreateOwner(std::make_shared<int>(5));
    Retiring cross{&other,&b};
    assert(r.Subscribe(a,RetireInside,&cross));
    assert(!r.Dispatch() && cross.called);
    assert(r.Retire(a) && other.Retire(b));

    a=r.CreateOwner(std::make_shared<int>(6));
    assert(r.Subscribe(a,Throw,nullptr));
    assert(r.Subscribe(a,Count,&calls));
    assert(r.Dispatch()==1 && calls==4);
    assert(!r.Dispatch() && !r.Subscribe(a,Count,&calls));
    assert(r.Retire(a));

    // Publication, parallel dispatch and drain: after drain no late call enters.
    a=r.CreateOwner(std::make_shared<int>(7));
    std::array<std::atomic<unsigned>,32> counters{};
    std::array<std::thread,8> workers;
    assert(r.Subscribe(a,Count,&counters[0]));
    for (auto& w:workers) w=std::thread([&] { for(int i=0;i<1000;++i) assert(!r.Dispatch()); });
    for (unsigned i=1;i<counters.size();++i) assert(r.Subscribe(a,Count,&counters[i]));
    for(auto& w:workers) w.join();
    assert(counters[0]==8000);
    assert(r.Retire(a));
    const auto before=counters[0].load();
    assert(!r.Dispatch() && counters[0]==before);

    std::array<std::atomic<unsigned>,Registry::Capacity+1> full{};
    a=r.CreateOwner(std::make_shared<int>(8));
    for(unsigned i=0;i<Registry::Capacity;++i) assert(r.Subscribe(a,Count,&full[i]));
    assert(r.Subscribe(a,Count,&full[0]));
    assert(!r.Subscribe(a,Count,&full.back()));
    assert(!r.Dispatch());
    for(unsigned i=0;i<Registry::Capacity;++i) assert(full[i]==1);
    assert(full.back()==0 && r.Retire(a));

    // A real DSO remains resident through the complete call and snapshot.
    void* handle=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);assert(handle);
    auto fn=reinterpret_cast<BbPluginCallbacks::Callback>(dlsym(handle,"cachyborne_test_plugin_callback"));
    assert(fn);
    std::atomic<bool> unloaded{false},drained{false};
    auto pin=std::shared_ptr<void>(handle,[&](void* h) { assert(dlclose(h)==0);unloaded=true; });
    a=r.CreateOwner(std::move(pin));
    Blocked blocked;CallbackFixture fixture{Block,&blocked};
    assert(r.Subscribe(a,fn,&fixture));
    std::thread running([&] { assert(!r.Dispatch()); });
    blocked.entered.acquire();
    std::thread retiring([&] { assert(r.Retire(a));drained=true; });
    while(r.Subscribe(a,fn,&fixture)) std::this_thread::yield();
    assert(!drained && !unloaded);
    // Concurrent dispatch after admission closes cannot enter this blocked DSO.
    assert(!r.Dispatch() && blocked.calls==1);
    blocked.release.release();running.join();retiring.join();
    assert(drained && !unloaded);
    a={};assert(unloaded);
    assert(!dlopen(argv[1],RTLD_NOW|RTLD_NOLOAD));

    // Self-retirement may drop the host owner while control must still return
    // through plugin code. The snapshot pin prevents unloading that return PC.
    handle=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);assert(handle);
    fn=reinterpret_cast<BbPluginCallbacks::Callback>(dlsym(handle,"cachyborne_test_plugin_callback"));
    assert(fn);unloaded=false;
    a=r.CreateOwner(std::shared_ptr<void>(handle,[&](void* h) { assert(dlclose(h)==0);unloaded=true; }));
    struct SelfDrop { Registry* registry;Registry::Owner* owner;std::atomic<bool>* unloaded; };
    SelfDrop drop{&r,&a,&unloaded};
    fixture={[](void* p) {
        auto& self=*static_cast<SelfDrop*>(p);
        assert(!self.registry->Retire(*self.owner));
        *self.owner={};
        assert(!*self.unloaded);
    },&drop};
    assert(r.Subscribe(a,fn,&fixture));
    assert(!r.Dispatch() && unloaded);
    assert(!dlopen(argv[1],RTLD_NOW|RTLD_NOLOAD));
}
