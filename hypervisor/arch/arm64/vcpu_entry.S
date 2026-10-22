/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
/* vCPU 出入口。偏移契约见 vcpu.h:gprs[31]@0 sp@248 pc@256 pstate@264
 * host_ctx@272(x19..x28@272-344,sp@352,lr@360)。 */
.section .text

/* ---- 陷阱入口:lower-EL sync 向量槽 b 进来,SPSel=1(sp=SP_EL2=excp_stack)
 * x18-x28 属 guest,禁作 scratch;用 excp_stack 停 x0 完成引导 ---- */
.global el1_trap
el1_trap:
    str     x0, [sp, #-16]!
    adrp    x0, cur_vcpu
    ldr     x0, [x0, :lo12:cur_vcpu]
    stp     x1, x2,  [x0, #8]
    stp     x3, x4,  [x0, #24]
    stp     x5, x6,  [x0, #40]
    stp     x7, x8,  [x0, #56]
    stp     x9, x10, [x0, #72]
    stp     x11, x12, [x0, #88]
    stp     x13, x14, [x0, #104]
    stp     x15, x16, [x0, #120]
    stp     x17, x18, [x0, #136]
    stp     x19, x20, [x0, #152]
    stp     x21, x22, [x0, #168]
    stp     x23, x24, [x0, #184]
    stp     x25, x26, [x0, #200]
    stp     x27, x28, [x0, #216]
    stp     x29, x30, [x0, #232]
    ldr     x1, [sp], #16             /* guest x0 回帧 */
    str     x1, [x0]
    mrs     x1, sp_el1
    str     x1, [x0, #248]
    mrs     x1, elr_el2
    str     x1, [x0, #256]
    mrs     x1, spsr_el2
    str     x1, [x0, #264]
    b       el1_sync                  /* x0 = vcpu;此路径不再返回 */

/* ---- 续跑:帧即真相 -> 加载 -> eret(noreturn) ---- */
.global trap_resume
trap_resume:                          /* x0 = vcpu */
    ldr     x1, [x0, #248]
    msr     sp_el1, x1
    ldr     x1, [x0, #256]
    msr     elr_el2, x1
    ldr     x1, [x0, #264]
    msr     spsr_el2, x1
    ldp     x1, x2,  [x0, #8]
    ldp     x3, x4,  [x0, #24]
    ldp     x5, x6,  [x0, #40]
    ldp     x7, x8,  [x0, #56]
    ldp     x9, x10, [x0, #72]
    ldp     x11, x12, [x0, #88]
    ldp     x13, x14, [x0, #104]
    ldp     x15, x16, [x0, #120]
    ldp     x17, x18, [x0, #136]
    ldp     x19, x20, [x0, #152]
    ldp     x21, x22, [x0, #168]
    ldp     x23, x24, [x0, #184]
    ldp     x25, x26, [x0, #200]
    ldp     x27, x28, [x0, #216]
    ldp     x29, x30, [x0, #232]
    ldr     x1, [x0]                  /* guest x0 最后回填 */
    mov     x0, x1
    eret

/* ---- 进入 guest:存宿主现场 -> 复用帧加载 -> eret(noreturn)
 * 正常"返回"= guest 已 retire,vcpu_return_host 恢复此处保存的现场 ---- */
.global vcpu_enter_guest
vcpu_enter_guest:                     /* x0 = vcpu */
    stp     x19, x20, [x0, #272]
    stp     x21, x22, [x0, #288]
    stp     x23, x24, [x0, #304]
    stp     x25, x26, [x0, #320]
    stp     x27, x28, [x0, #336]
    mov     x16, sp
    mov     x17, x30
    stp     x16, x17, [x0, #352]
    b       trap_resume               /* 帧加载尾部复用(新跑=零帧,续跑=断点) */

/* ---- 还宿主:恢复 host_ctx 后 ret 回 vcpu_run 调用点 ---- */
.global vcpu_return_host
vcpu_return_host:                     /* x0 = vcpu */
    ldp     x19, x20, [x0, #272]
    ldp     x21, x22, [x0, #288]
    ldp     x23, x24, [x0, #304]
    ldp     x25, x26, [x0, #320]
    ldp     x27, x28, [x0, #336]
    ldp     x16, x17, [x0, #352]
    msr     spsel, #0
    mov     sp, x16
    ret     x17
