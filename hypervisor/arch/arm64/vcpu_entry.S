/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
/* ERET 进 guest 与 exit 恢复 host 帧。偏移见 vcpu.h(entry_pc@0
 * entry_sp@8 spsr@16 host_ctx.sp@24 host_ctx.lr@32)。 */
.section .text
.global vcpu_eret_enter

/* void vcpu_eret_enter(struct vcpu *v) — x0=v;不返回
 * (exit 路径经 vcpu_return_host 恢复帧后 ret 回 vcpu_run) */
vcpu_eret_enter:
    mov     x16, sp
    mov     x17, x30
    stp     x16, x17, [x0, #24]
    eret

.global vcpu_return_host

/* noreturn — x0=v;恢复 host sp/lr 并 ret 回 vcpu_run 调用点。
 * 异常进入 EL2 时硬件强制 SPSel=1(栈=SP_EL2),而宿主 C 世界住在
 * SPSel=0/SP_EL0 bank——先切回 bank 0 再写 sp,才真正还回宿主栈
 * (guest 自身用 SP_EL1,与本 bank 无关)。 */
vcpu_return_host:
    ldp     x16, x17, [x0, #24]
    msr     spsel, #0
    mov     sp, x16
    ret     x17
