// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_guest_code_hooks.h"
#include <array>
#include <cassert>
#include <cstring>
#include <thread>
#include <vector>
#include <sys/mman.h>

using Fn = void (*)(int*);
Fn run;
unsigned observed = 0;
void __attribute__((sysv_abi)) Observe(std::uint64_t, const Core::GuestRegisterSnapshot*) {
    ++observed;
}
void* original_run;
void* original_result;
void __attribute__((sysv_abi)) Replace(std::uint64_t, const Core::GuestRegisterSnapshot* regs) {
    auto* value = reinterpret_cast<int*>(regs->rdi);
    if (*value == -1) reinterpret_cast<Fn>(original_run)(value);
    *value += 10;
    if (*value == 10) run(value); // recursion through the same trampoline
}

void __attribute__((sysv_abi)) ReplaceResult(std::uint64_t, const Core::GuestRegisterSnapshot* regs) {
    const auto value = reinterpret_cast<int (*)()>(original_result)();
    const_cast<Core::GuestRegisterSnapshot*>(regs)->rax = value + 5;
}
int main() {
    auto* code = static_cast<unsigned char*>(mmap(nullptr, 4096, PROT_READ | PROT_WRITE,
                                                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    assert(code != MAP_FAILED);
    // Original: inc dword [rdi]; three nops; ret. A replacement must never
    // execute the increment, on the first call or subsequent/concurrent calls.
    constexpr std::array<unsigned char, 5> entry{0xff,0x07,0x90,0x90,0x90};
    std::memcpy(code, entry.data(), entry.size());
    code[5] = 0xc3;
    constexpr std::array<unsigned char,5> result_entry{0xb8,0x07,0x00,0x00,0x00};
    std::memcpy(code+64,result_entry.data(),result_entry.size());
    code[69] = 0xc3;
    std::memset(code+128,0x90,69);
    code[197] = 0xc3;
    assert(mprotect(code,4096,PROT_READ | PROT_EXEC)==0);
    run = reinterpret_cast<Fn>(code);
    int original = 0;
    run(&original);
    assert(original == 1);
    assert(Core::InstallGuestCodeHook(code, entry, 0, Replace, nullptr, true, &original_run));
    assert(Core::InstallGuestCodeHook(code+64, result_entry, 0, ReplaceResult, nullptr, true, &original_result));
    std::array<unsigned char,64> long_entry;
    long_entry.fill(0x90);
    constexpr std::array<unsigned char,5> short_entry{0x90,0x90,0x90,0x90,0x90};
    assert(Core::InstallGuestCodeHook(code+128,long_entry,0,Observe));
    // Padding inside the installed span still contains valid NOPs. A guessed
    // 16-byte exclusion would miss this real overlap with a longer detour.
    assert(!Core::InstallGuestCodeHook(code+152,short_entry,0,Observe));
    // Exactly adjacent instruction spans are safe and both observers run.
    assert(Core::InstallGuestCodeHook(code+192,short_entry,0,Observe));
    reinterpret_cast<void (*)()>(code+128)();
    assert(observed==2);
    for (int i=0;i<1000;++i) assert(reinterpret_cast<int (*)()>(code+64)() == 12);
    int original_path=-1;
    run(&original_path);
    assert(original_path == 20);
    std::array<int, 8> values{};
    std::vector<std::jthread> workers;
    for (auto& value : values) workers.emplace_back([&value] {
        for (int i = 0; i < 1000; ++i) run(&value);
    });
    workers.clear();
    for (int value : values) assert(value == 10010);
    munmap(code,4096);
}
