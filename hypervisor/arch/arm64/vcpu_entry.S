/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
/* ERET 进 guest 与 exit 恢复 host 帧。偏移见 vcpu.h(entry_pc@0
 * entry_sp@8 spsr@16 host_ctx@24: x19-x28@24-72 sp@104 lr@112)。 */
.section .text
.global vcpu_eret_enter

/* void vcpu_eret_enter(struct vcpu *v) — x0=v;不返回
 * (exit 路径经 vcpu_return_host 恢复帧后 ret 回 vcpu_run 调用点)
 * ERET 后 guest 覆盖一切可覆盖寄存器 → 宿主现场必须整块入 v:
 * callee-saved(x19-x28) + SP + LR。x16/x17 作临时,EL2 下安全。 */
vcpu_eret_enter:
    stp     x19, x20, [x0, #24]
    stp     x21, x22, [x0, #40]
    stp     x23, x24, [x0, #56]
    stp     x25, x26, [x0, #72]
    stp     x27, x28, [x0, #88]
    mov     x16, sp
    mov     x17, x30
    stp     x16, x17, [x0, #104]
    eret

.global vcpu_return_host

/* noreturn — x0=v;恢复宿主完整现场后 ret 回 vcpu_run 调用点。
 * 异常进入 EL2 时硬件强制 SPSel=1(栈=SP_EL2),而宿主 C 世界住在
 * SPSel=0/SP_EL0 bank——先切回 bank 0 再写 sp,才真正还回宿主栈
 * (guest 自身用 SP_EL1,与本 bank 无关)。 */
vcpu_return_host:
    ldp     x19, x20, [x0, #24]
    ldp     x21, x22, [x0, #40]
    ldp     x23, x24, [x0, #56]
    ldp     x25, x26, [x0, #72]
    ldp     x27, x28, [x0, #88]
    ldp     x16, x17, [x0, #104]
    msr     spsel, #0
    mov     sp, x16
    ret     x17
