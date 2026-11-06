// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_guest_code_hooks.h"
#include <array>
#include <barrier>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

using Fn = std::uint64_t (*)(std::uint64_t);
Fn run;
void* original;
std::barrier rendezvous(8);
thread_local bool concurrent = false;

bool __attribute__((sysv_abi)) Decide(std::uint64_t tag,
                                      const Core::GuestRegisterSnapshot* input) {
    assert(tag == 0xabc123);
    auto& regs = *const_cast<Core::GuestRegisterSnapshot*>(input);
    const auto value = regs.rdi;
    if (concurrent) rendezvous.arrive_and_wait();
    if (value == 999) {
        // Opposite decisions nested inside one callback must not affect the
        // parent. The callable original bypasses the observer entirely.
        assert(run(2) == 13);
        assert(run(3) == 1003);
        assert(reinterpret_cast<Fn>(original)(7) == 8);
        regs.rax = 1999;
        return true;
    }
    if (value & 1) {
        regs.rax = value + 1000;
        return true;
    }
    regs.rdi = value + 10;
    return false;
}

static void CheckFlags(Fn fn, std::uint64_t value, bool carry) {
    std::uint64_t result;
    unsigned char after;
    // The body consists of LEA/NOP/RET, so both paths must preserve incoming
    // carry even though the host callback and trampoline branch change it.
    if (carry) {
        asm volatile("stc; call *%[fn]; setc %[after]"
                     : "=a"(result), [after] "=m"(after), "+D"(value)
                     : [fn] "r"(fn)
                     : "rcx", "rdx", "rsi", "r8", "r9", "r10", "r11", "cc", "memory");
    } else {
        asm volatile("clc; call *%[fn]; setc %[after]"
                     : "=a"(result), [after] "=m"(after), "+D"(value)
                     : [fn] "r"(fn)
                     : "rcx", "rdx", "rsi", "r8", "r9", "r10", "r11", "cc", "memory");
    }
    assert(bool(after) == carry);
    assert(result == (value & 1 ? value + 1000 : value + 1));
}

int main() {
    auto* code = static_cast<unsigned char*>(mmap(nullptr, 4096, PROT_READ | PROT_WRITE,
                                                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    assert(code != MAP_FAILED);
    constexpr std::array<unsigned char, 5> entry{0x48,0x8d,0x47,0x01,0x90};
    std::memcpy(code, entry.data(), entry.size());
    code[5] = 0xc3;
    assert(mprotect(code,4096,PROT_READ | PROT_EXEC) == 0);
    run = reinterpret_cast<Fn>(code);
    assert(run(7) == 8);
    assert(!Core::InstallConditionalGuestCodeHook(code,entry,0,nullptr));
    assert(Core::InstallConditionalGuestCodeHook(code,entry,0xabc123,Decide,&original));
    assert(run(2) == 13);
    assert(run(3) == 1003);
    assert(run(999) == 1999);
    CheckFlags(run,2,false);
    CheckFlags(run,2,true);
    CheckFlags(run,3,false);
    CheckFlags(run,3,true);
    std::vector<std::jthread> workers;
    for (unsigned worker=0; worker<8; ++worker) workers.emplace_back([worker] {
        concurrent = true;
        for (unsigned iteration=0; iteration<2000; ++iteration) {
            const auto value = std::uint64_t(worker+2);
            assert(run(value) == (value & 1 ? value+1000 : value+11));
        }
    });
    workers.clear();
    assert(reinterpret_cast<Fn>(original)(123) == 124);
    // Shared mapping of a read-only file descriptor cannot gain write access.
    // Refusal must leave a callable original and no published hook/trampoline.
    char name[]="hook-readonly-XXXXXX";
    const int writable=mkstemp(name);assert(writable>=0);
    assert(!ftruncate(writable,4096));
    std::array<unsigned char,6> body{0x48,0x8d,0x47,0x01,0x90,0xc3};
    assert(write(writable,body.data(),body.size())==ssize_t(body.size()));
    const int readonly=open(name,O_RDONLY);assert(readonly>=0);
    assert(!unlink(name) && !close(writable));
    auto* denied=static_cast<unsigned char*>(mmap(nullptr,4096,PROT_READ|PROT_EXEC,MAP_SHARED,readonly,0));
    assert(denied!=MAP_FAILED);assert(!close(readonly));
    void* refused_original=reinterpret_cast<void*>(1);
    assert(!Core::InstallConditionalGuestCodeHook(denied,entry,0xabc123,Decide,&refused_original));
    assert(!refused_original && !std::memcmp(denied,body.data(),body.size()));
    assert(reinterpret_cast<Fn>(denied)(7)==8);
    assert(!Core::InstallConditionalGuestCodeHook(denied,entry,0xabc123,Decide));
    assert(!munmap(denied,4096));
    assert(!munmap(code,4096));
}
