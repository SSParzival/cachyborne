// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_guest_call.h"
#include <array>
#include <bit>
#include <cassert>
#include <cstdarg>
#include <stdexcept>
#include <thread>

using U = std::uint64_t;
using BbGuestCall::Registers;
namespace {
struct MixedResult { U integer; double real; };
__attribute__((noinline)) MixedResult Mixed(
    U a, U b, U c, U d, U e, U f, U g, U h, U i, U j,
    double x0, double x1, double x2, double x3,
    double x4, double x5, double x6, double x7) {
    // Weighted terms detect swapped register/stack slots, not only lost values.
    return {a+2*b+3*c+4*d+5*e+6*f+7*g+8*h+9*i+10*j,
            x0+2*x1+3*x2+4*x3+5*x4+6*x5+7*x6+8*x7};
}
__attribute__((noinline)) float Float(float a, float b) { return a*3.f+b; }
__attribute__((noinline)) double Variadic(U first, ...) {
    va_list ap; va_start(ap,first);
    double sum=static_cast<double>(first);
    for (int i=0;i<9;++i) sum+=static_cast<double>(va_arg(ap,U))*(i+2);
    for (int i=0;i<8;++i) sum+=va_arg(ap,double)*(i+1);
    va_end(ap); return sum;
}
extern "C" U bb_test_call_alignment();
asm(".text\n.globl bb_test_call_alignment\nbb_test_call_alignment:\n"
    "mov %rsp,%rax\nand $15,%rax\nret\n");
__attribute__((noinline)) U Recurse(U depth) {
    if (!depth) return 7;
    Registers r{};r.fn=reinterpret_cast<void*>(&Recurse);r.ints[0]=depth-1;
    assert(BbGuestCall::Invoke(r));return r.ret+depth;
}
__attribute__((noinline)) U Throw() { throw std::runtime_error("bridge unwind"); }
void Check() {
    Registers r{};r.fn=reinterpret_cast<void*>(&Mixed);
    for (U i=0;i<6;++i) r.ints[i]=i+1;
    for (U i=0;i<4;++i) r.stack[i]=i+7;
    for (U i=0;i<8;++i) r.xmm[i]=std::bit_cast<U>(double(i+1));
    assert(BbGuestCall::Invoke(r));
    assert(r.ret==385 && std::bit_cast<double>(r.ret_xmm0)==204.);
    r.fn=reinterpret_cast<void*>(&Variadic);
    assert(BbGuestCall::Invoke(r));
    assert(std::bit_cast<double>(r.ret_xmm0)==589.);
    r={};r.fn=reinterpret_cast<void*>(&Float);
    r.xmm[0]=std::bit_cast<std::uint32_t>(1.25f);
    r.xmm[1]=std::bit_cast<std::uint32_t>(2.5f);
    assert(BbGuestCall::Invoke(r));
    assert(std::bit_cast<float>(std::uint32_t(r.ret_xmm0))==6.25f);
    r.fn=reinterpret_cast<void*>(&bb_test_call_alignment);
    assert(BbGuestCall::Invoke(r) && r.ret==8);
    r.fn=reinterpret_cast<void*>(&Recurse);r.ints[0]=12;
    assert(BbGuestCall::Invoke(r) && r.ret==85);
}
}
int main() {
    Registers empty{};empty.ret=19;empty.ret_xmm0=23;
    assert(!BbGuestCall::Invoke(empty) && empty.ret==19 && empty.ret_xmm0==23);
    Check();
    std::array<std::thread,8> workers;
    for (auto& worker:workers) worker=std::thread([] { for(int i=0;i<1000;++i) Check(); });
    for (auto& worker:workers) worker.join();
    // This validates bridge CFI with a host target, not guest exception semantics.
    Registers r{};r.fn=reinterpret_cast<void*>(&Throw);
    bool caught=false;
    try { BbGuestCall::Invoke(r); } catch (const std::runtime_error&) { caught=true; }
    assert(caught);
}
