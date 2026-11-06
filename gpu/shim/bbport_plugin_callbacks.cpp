// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_plugin_callbacks.h"
#include "bbport_frame_tasks.h"
#include <algorithm>
#include <condition_variable>
#include <cstdio>

namespace BbPluginCallbacks {
struct Registry::State {
    std::shared_ptr<Identity> identity;
    std::shared_ptr<void> module;
    std::mutex mutex;
    std::condition_variable drained;
    DisableAction disable_action;
    bool disable_running=false;
    bool active = true;
    unsigned running = 0;
};
namespace {
// Shared across registries: waiting on another registry from a callback can
// deadlock just as waiting on another owner in this registry can.
thread_local unsigned dispatch_depth = 0;
thread_local unsigned disable_depth = 0;
thread_local Registry::Scope* current_scope = nullptr;
void Frame() {
    const unsigned failures = Frames().Dispatch();
    if (failures) std::printf("Plugin frame callbacks: %u exception(s); owners disabled\n", failures);
}
}
Registry::Registry() : identity(std::make_shared<Identity>()),
    entries(std::make_shared<const Entries>()) {}

Registry::Invocation::Invocation(const Owner& owner) : state(owner.state) {
    if (!state) return;
    std::lock_guard gate{state->mutex};
    if (!state->active) return;
    ++state->running;++dispatch_depth;entered=true;
}
Registry::Invocation::~Invocation() {
    if (!entered) return;
    --dispatch_depth;
    std::lock_guard gate{state->mutex};
    if (--state->running==0) state->drained.notify_all();
}
void Registry::Invocation::Quarantine() {
    if (!entered) return;
    Registry::Disable(state);
}

void Registry::Disable(const std::shared_ptr<State>& state) {
    {
        std::lock_guard gate{state->mutex};
        if (!state->active) return;
        state->active=false;
        if (!state->disable_action.run) return;
        state->disable_running=true;
    }
    // A resource callback can retire another owner, or even this one. Keep
    // those calls nonblocking to avoid self/cross-owner cleanup deadlocks.
    ++disable_depth;
    state->disable_action.run(state->disable_action.context.get());
    --disable_depth;
    {
        std::lock_guard gate{state->mutex};
        state->disable_running=false;state->drained.notify_all();
    }
}

Registry::Scope::Scope(Registry& r, const Owner& o) : registry(&r), owner(o) {
    // An invalid inner scope must mask its outer owner, never accidentally
    // register a different plugin's callback against the outer module.
    if (!owner.state || owner.state->identity != r.identity) owner={};
    previous = current_scope;
    current_scope = this;
    installed = true;
}
Registry::Scope::~Scope() {
    if (installed) current_scope = previous;
}
bool Registry::SubscribeCurrent(Callback callback, void* user) {
    return current_scope && current_scope->registry->Subscribe(current_scope->owner, callback, user);
}
Registry::Context Registry::Current() {
    if (!current_scope || !current_scope->owner.state) return {};
    return {current_scope->registry,current_scope->owner};
}

Registry::Owner Registry::CreateOwner(std::shared_ptr<void> module_pin) {
    return CreateOwner(std::move(module_pin),{});
}
Registry::Owner Registry::CreateOwner(std::shared_ptr<void> module_pin,DisableAction action) {
    Owner owner;
    if (!module_pin) return owner;
    owner.state = std::make_shared<State>();
    owner.state->identity = identity;
    owner.state->module = std::move(module_pin);
    owner.state->disable_action=std::move(action);
    return owner;
}

bool Registry::Subscribe(const Owner& owner, Callback callback, void* user) {
    if (!owner.state || owner.state->identity != identity || !callback) return false;
    std::lock_guard publish{publication};
    std::lock_guard gate{owner.state->mutex};
    if (!owner.state->active) return false;
    const auto old = entries.load(std::memory_order_acquire);
    for (const auto& entry : *old)
        if (entry.state == owner.state && entry.callback == callback && entry.user == user) return true;
    if (old->size() >= Capacity) return false;
    auto next = std::make_shared<Entries>(*old);
    next->push_back({owner.state, callback, user});
    entries.store(std::move(next), std::memory_order_release);
    return true;
}

bool Registry::Retire(const Owner& owner) {
    if (!owner.state || owner.state->identity != identity) return false;
    const auto state=owner.state;
    Disable(state);
    {
        std::lock_guard publish{publication};
        const auto old = entries.load(std::memory_order_acquire);
        if (std::any_of(old->begin(), old->end(), [&](const auto& e) { return e.state == state; })) {
            auto next = std::make_shared<Entries>();
            next->reserve(old->size());
            for (const auto& entry : *old) if (entry.state != state) next->push_back(entry);
            entries.store(std::move(next), std::memory_order_release);
        }
    }
    if (dispatch_depth || disable_depth) return false;
    std::unique_lock gate{state->mutex};
    state->drained.wait(gate, [&] { return state->running == 0 && !state->disable_running; });
    return true;
}

unsigned Registry::Dispatch() noexcept {
    const auto current = entries.load(std::memory_order_acquire);
    unsigned failures = 0;
    for (const auto& entry : *current) {
        Owner owner;owner.state=entry.state;
        Invocation invocation{owner};
        if (!invocation) continue;
        Scope context{*this,owner};
        try { entry.callback(entry.user); }
        catch (...) { invocation.Quarantine();++failures; }
    }
    return failures;
}

Registry& Frames() {
    // Registered frame task can outlive host exit callbacks.
    static auto* const registry = new Registry;
    return *registry;
}
bool InstallFrames(unsigned char* image, std::uint64_t size) {
    return BbFrameTasks::Subscribe(image, size, Frame);
}
}
