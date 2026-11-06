// SPDX-License-Identifier: GPL-3.0-or-later
// Adapted from BBHOST core/thunk.cpp hle_call_guest_regs. Native guest TLS
// lives in GS; host FS remains installed, so no BBHOST FS switching is needed.
#include "bbport_guest_call.h"

extern "C" __attribute__((sysv_abi)) void bb_native_call_guest_regs(BbGuestCall::Registers*);
#if defined(_WIN32)
#define BB_CALL_SYMBOL_TYPE ""
#define BB_CALL_SYMBOL_SIZE ""
#else
#define BB_CALL_SYMBOL_TYPE ".hidden bb_native_call_guest_regs\n.type bb_native_call_guest_regs,@function\n"
#define BB_CALL_SYMBOL_SIZE ".size bb_native_call_guest_regs, .-bb_native_call_guest_regs\n"
#endif
asm(R"(
    .text
    .globl bb_native_call_guest_regs
)" BB_CALL_SYMBOL_TYPE R"(
bb_native_call_guest_regs:
    .cfi_startproc
    push %rbp
    .cfi_def_cfa_offset 16
    .cfi_offset %rbp, -16
    mov %rsp, %rbp
    .cfi_def_cfa_register %rbp
    push %rbx
    .cfi_offset %rbx, -24
    push %r12
    .cfi_offset %r12, -32
    mov %rdi, %rbx
    sub $32, %rsp
    mov 120(%rbx), %r11
    mov %r11, 0(%rsp)
    mov 128(%rbx), %r11
    mov %r11, 8(%rsp)
    mov 136(%rbx), %r11
    mov %r11, 16(%rsp)
    mov 144(%rbx), %r11
    mov %r11, 24(%rsp)
    mov 8(%rbx), %rdi
    mov 16(%rbx), %rsi
    mov 24(%rbx), %rdx
    mov 32(%rbx), %rcx
    mov 40(%rbx), %r8
    mov 48(%rbx), %r9
    movq 56(%rbx), %xmm0
    movq 64(%rbx), %xmm1
    movq 72(%rbx), %xmm2
    movq 80(%rbx), %xmm3
    movq 88(%rbx), %xmm4
    movq 96(%rbx), %xmm5
    movq 104(%rbx), %xmm6
    movq 112(%rbx), %xmm7
    mov $8, %eax
    call *0(%rbx)
    add $32, %rsp
    mov %rax, 152(%rbx)
    movq %xmm0, 160(%rbx)
    pop %r12
    .cfi_restore %r12
    pop %rbx
    .cfi_restore %rbx
    pop %rbp
    .cfi_restore %rbp
    .cfi_def_cfa %rsp, 8
    ret
    .cfi_endproc
)" BB_CALL_SYMBOL_SIZE R"(
)");

bool BbGuestCall::Invoke(Registers& registers) {
    if (!registers.fn) return false;
    bb_native_call_guest_regs(&registers);
    return true;
}
