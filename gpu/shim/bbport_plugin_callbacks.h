// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace BbPluginCallbacks {
using Callback = void (*)(void*);
// A module pin must own the dlopen reference (or equivalent lifetime resource).
// It is retained by owners, published entries and in-flight dispatch snapshots.
// These pins cover callbacks only; hooks/HLE/threads need their own residency.
class Registry {
    struct Identity {};
    struct State;
public:
    class Owner {
        friend class Registry;
        std::shared_ptr<State> state;
    };
    struct Context { Registry* registry=nullptr;Owner owner; };
    struct DisableAction {
        void (*run)(void*) noexcept=nullptr;
        std::shared_ptr<void> context;
    };
    // All owner callbacks (frames, hooks, future adapters) use this same gate
    // and in-flight count. Retire drains them together and refuses to wait
    // inside any invocation, including callbacks entered through guest hooks.
    class Invocation {
        std::shared_ptr<State> state;
        bool entered=false;
    public:
        explicit Invocation(const Owner&);
        ~Invocation();
        Invocation(const Invocation&) = delete;
        Invocation& operator=(const Invocation&) = delete;
        explicit operator bool() const { return entered; }
        void Quarantine();
    };
    // Associate host API calls with their plugin during init/image phases.
    // Dispatch also establishes this context, so registrations made inside a
    // callback inherit its owner. Scopes nest; they must leave in stack order.
    class Scope {
        friend class Registry;
        Registry* registry;
        Owner owner;
        Scope* previous = nullptr;
        bool installed = false;
    public:
        Scope(Registry&, const Owner&);
        ~Scope();
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
    };
    static constexpr unsigned Capacity = 256;
    Registry();
    Registry(const Registry&) = delete;
    Registry& operator=(const Registry&) = delete;
    // The disable action runs once when admission closes, outside registry locks.
    // It must not throw or free userdata still used by active calls. Retirement
    // called from a disable action never waits; a
    // caller outside callbacks/actions drains invocations and the disable action.
    Owner CreateOwner(std::shared_ptr<void> module_pin);
    Owner CreateOwner(std::shared_ptr<void> module_pin,DisableAction action);
    // Userdata must remain valid through retirement/drain, or be owned by the
    // module pin. A DSO pin alone does not retain plugin-allocated user objects.
    bool Subscribe(const Owner&, Callback, void* user);
    static bool SubscribeCurrent(Callback, void* user);
    static Context Current();
    // Disable admission and remove all entries of this owner. Outside any
    // registry callback this waits for running callbacks and returns true.
    // Inside any callback it returns false without waiting, avoiding self and
    // cross-owner deadlocks. Call again outside dispatch to finish draining.
    // A false result must never be interpreted as permission to force dlclose.
    bool Retire(const Owner&);
    // Snapshot iteration allocates nothing. New registrations appear in a
    // subsequent snapshot. Exceptions quarantine the owner and are counted;
    // the host must later Retire it to remove entries and release module pins.
    unsigned Dispatch() noexcept;
private:
    struct Entry { std::shared_ptr<State> state; Callback callback; void* user; };
    static void Disable(const std::shared_ptr<State>&);
    using Entries = std::vector<Entry>;
    std::shared_ptr<Identity> identity;
    std::mutex publication;
    std::atomic<std::shared_ptr<const Entries>> entries;
};

// The plugin host explicitly installs this adapter only when needed. Existing
// process-lifetime frame tasks are unchanged; no plugin task is installed by
// the normal game setup in the absence of a plugin host.
Registry& Frames();
bool InstallFrames(unsigned char* image, std::uint64_t size);
}
