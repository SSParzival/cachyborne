// SPDX-License-Identifier: GPL-3.0-or-later
#include "bbport_plugin_hooks.h"
#include "bbport_guest_code_hooks.h"
#include "bbport_plugin_callbacks.h"
#include "bbport_plugin_module.h"
#include "common/guest_memory.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

namespace BbPluginHooks {
namespace {
using Registry=BbPluginCallbacks::Registry;
struct Binding {
    Registry::Context context;
    BbHookFn first;
    BbHookFn2 second;
    void* user;
};
struct Bindings { std::mutex mutex;std::vector<std::unique_ptr<Binding>> entries; };
Bindings& Get() { static auto* const bindings=new Bindings;return *bindings; }
bool __attribute__((sysv_abi)) Dispatch(std::uint64_t tag,const Core::GuestRegisterSnapshot* saved) noexcept {
    const auto& binding=*reinterpret_cast<const Binding*>(tag);
    Registry::Invocation invocation{binding.context.owner};
    if (!invocation) return false;
    Registry::Scope context{*binding.context.registry,binding.context.owner};
    auto& r=*const_cast<Core::GuestRegisterSnapshot*>(saved);
    try {
        if (binding.second) {
            const std::array<std::uint64_t*,6> args{&r.rdi,&r.rsi,&r.rdx,&r.rcx,&r.r8,&r.r9};
            BbHookCtx call{};
            for (unsigned i=0;i<6;++i) call.arg[i]=*args[i];
            for (unsigned i=0;i<8;++i) std::memcpy(&call.xmm[i],r.ymm[i].data(),8);
            const bool skip=binding.second(&call,binding.user)!=0;
            // Commit only after a normal return: a throwing callback cannot
            // leave partially rewritten arguments in the original function.
            for (unsigned i=0;i<6;++i) *args[i]=call.arg[i];
            for (unsigned i=0;i<8;++i) std::memcpy(r.ymm[i].data(),&call.xmm[i],8);
            if (skip) r.rax=call.ret;
            return skip;
        }
        const BbHookFrame call{r.rdi,r.rsi,r.rdx,r.rcx,r.r8,r.r9};
        const bool skip=binding.first(&call,binding.user)!=0;
        if (skip) r.rax=0;
        return skip;
    } catch (...) {
        invocation.Quarantine();
        std::fputs("Plugin hook callback threw; owner disabled, original function retained\n",stderr);
        return false;
    }
}
}
int Hooks::Hook(std::uint64_t bn,const void* prologue,std::size_t bytes,BbHookFn callback,void* user) {
    if (!callback || !prologue) return 1;
    return Install(bn,prologue,bytes,callback,nullptr,user);
}
int Hooks::Hook2(std::uint64_t bn,const void* prologue,std::size_t bytes,BbHookFn2 callback,void* user) {
    if (!callback) return 1;
    return Install(bn,prologue,bytes,nullptr,callback,user);
}
int Hooks::Install(std::uint64_t bn,const void* prologue,std::size_t bytes,BbHookFn first,BbHookFn2 second,void* user) {
    if (!BbPluginModule::Module::CanInstallHooks() || bytes<14 || bytes>64) return 1;
    const auto target=image.GuestAddress(bn,bytes);
    const auto context=Registry::Current();
    if (!target || !context.registry) return 1;
    Registry::Invocation admission{context.owner};
    if (!admission) return 1;
    std::array<std::uint8_t,64> actual{},expected{};
    if (!image.Read(bn,actual.data(),bytes)) return 1;
    if (prologue) {
        if (!BbGuestMemory::Read(nullptr,reinterpret_cast<std::uintptr_t>(prologue),expected.data(),bytes) ||
            std::memcmp(actual.data(),expected.data(),bytes)) return 1;
    } else expected=actual;
    auto& bindings=Get();std::lock_guard lock{bindings.mutex};
    if (bindings.entries.size()>=256) return 2;
    try {
        auto pending=std::make_unique<Binding>(Binding{context,first,second,user});
        const auto tag=reinterpret_cast<std::uintptr_t>(pending.get());
        // Retain before publication. If the core throws after publishing, the
        // binding remains valid and its quarantined owner becomes pass-through.
        bindings.entries.push_back(std::move(pending));
        if (!Core::InstallConditionalGuestCodeHook(reinterpret_cast<void*>(target),
            std::span<const std::uint8_t>(expected.data(),bytes),tag,Dispatch)) {
            bindings.entries.pop_back();return 2;
        }
        return 0;
    } catch (...) {
        admission.Quarantine();return 2;
    }
}
}
