/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
/* EL2 IRQ 入口:帧建在 SP_EL2 栈上(异常入 EL2 硬件强制 SPSel=1),
 * 对称恢复 —— 非 eret 弃帧路径,无栈泄漏(stop 3 教训的镜像面)。
 * ELR/SPSR_EL2 不动,C 分发返回后直接 eret。 */
.section .text
.global el2_trap
el2_trap:
    stp     x29, x30, [sp, #-256]!     /* 帧头(x29@0 x30@8)+ 预留 256B */
    stp     x0, x1,  [sp, #16]
    stp     x2, x3,  [sp, #32]
    stp     x4, x5,  [sp, #48]
    stp     x6, x7,  [sp, #64]
    stp     x8, x9,  [sp, #80]
    stp     x10, x11, [sp, #96]
    stp     x12, x13, [sp, #112]
    stp     x14, x15, [sp, #128]
    stp     x16, x17, [sp, #144]
    stp     x18, x19, [sp, #160]
    stp     x20, x21, [sp, #176]
    stp     x22, x23, [sp, #192]
    stp     x24, x25, [sp, #208]
    stp     x26, x27, [sp, #224]
    str     x28, [sp, #240]
    bl      el2_irq_host               /* C 分发:claim → handler → eoi */
    ldp     x0, x1,  [sp, #16]
    ldp     x2, x3,  [sp, #32]
    ldp     x4, x5,  [sp, #48]
    ldp     x6, x7,  [sp, #64]
    ldp     x8, x9,  [sp, #80]
    ldp     x10, x11, [sp, #96]
    ldp     x12, x13, [sp, #112]
    ldp     x14, x15, [sp, #128]
    ldp     x16, x17, [sp, #144]
    ldp     x18, x19, [sp, #160]
    ldp     x20, x21, [sp, #176]
    ldp     x22, x23, [sp, #192]
    ldp     x24, x25, [sp, #208]
    ldp     x26, x27, [sp, #224]
    ldr     x28, [sp, #240]
    ldp     x29, x30, [sp], #256
    eret

/* ---- 第五站:guest 运行态物理中断桩(lower-EL IRQ 槽专用) ----
 * 帧布局同 el2_trap(256B,建在 SP_EL2=excp_stack 栈顶,el1_trap 归位后
 * 每次入口必在栈顶;IRQ 进入硬件自动掩 I,无嵌套)。
 * 出口查 sched_need_resched:置位则帧转写进 cur_vcpu 陷阱帧后
 * vcpu_exit() 弃帧而走(与 HVC 退出同构,host_ctx.sp 重置宿主栈,
 * excp_stack 帧遗弃无害);对称恢复路径与 el2_trap 完全一致。 */
.global el2_trap_guest
el2_trap_guest:
    stp     x29, x30, [sp, #-256]!
    stp     x0, x1,  [sp, #16]
    stp     x2, x3,  [sp, #32]
    stp     x4, x5,  [sp, #48]
    stp     x6, x7,  [sp, #64]
    stp     x8, x9,  [sp, #80]
    stp     x10, x11, [sp, #96]
    stp     x12, x13, [sp, #112]
    stp     x14, x15, [sp, #128]
    stp     x16, x17, [sp, #144]
    stp     x18, x19, [sp, #160]
    stp     x20, x21, [sp, #176]
    stp     x22, x23, [sp, #192]
    stp     x24, x25, [sp, #208]
    stp     x26, x27, [sp, #224]
    str     x28, [sp, #240]
    bl      el2_irq_guest               /* C:ticker/量子递减/vtimer WOKE */
    ldr     x0, =sched_need_resched
    ldr     x1, [x0]
    cbz     x1, 9f
    bl      preempt_save_frame           /* 帧 -> cur_vcpu 陷阱帧 */
    /* PREEMPT 转写出口归位 SP(el1_trap 同款哲学,站十四战况:此口曾
     * 不还,vcpu_exit noreturn 后续跑泵链,SP 棘轮每发 guest-tick 净泄
     * 256B——裸机站 tick 节奏无感,Linux 全程 RUNNING,16s 碾穿 4KB
     * 栈底踩进 .data,cur_vcpu 遭灾成 guest 内核 VA,宿主崩于 EL2)。 */
    adrp    x0, excp_stack
    add     x0, x0, :lo12:excp_stack
    add     x0, x0, #0x1000             /* 栈顶 = excp_stack + 512*8 */
    mov     sp, x0
    bl      vcpu_exit                   /* noreturn:PREEMPT 让位 */
9:
    ldp     x0, x1,  [sp, #16]
    ldp     x2, x3,  [sp, #32]
    ldp     x4, x5,  [sp, #48]
    ldp     x6, x7,  [sp, #64]
    ldp     x8, x9,  [sp, #80]
    ldp     x10, x11, [sp, #96]
    ldp     x12, x13, [sp, #112]
    ldp     x14, x15, [sp, #128]
    ldp     x16, x17, [sp, #144]
    ldp     x18, x19, [sp, #160]
    ldp     x20, x21, [sp, #176]
    ldp     x22, x23, [sp, #192]
    ldp     x24, x25, [sp, #208]
    ldp     x26, x27, [sp, #224]
    ldr     x28, [sp, #240]
    ldp     x29, x30, [sp], #256
    eret

/* 帧转写:x29=x29/x30 以外的全量寄存器仍在帧上,用 x29(vcpu*)与 x0(临时)
 * 作工作寄存器(x30 保留 = ret 地址)。偏移契约 = vcpu.h/el1_trap 同款:
 * gprs[31]@0..240 sp@248 pc@256 pstate@264。 */
.global preempt_save_frame
preempt_save_frame:
    ldr     x29, =cur_vcpu
    ldr     x29, [x29]
    ldp     x0, x1,  [sp]               /* guest x29,x30(帧头) */
    stp     x0, x1,  [x29, #232]
    ldp     x0, x1,  [sp, #16]
    stp     x0, x1,  [x29]
    ldp     x2, x3,  [sp, #32]
    stp     x2, x3,  [x29, #16]
    ldp     x4, x5,  [sp, #48]
    stp     x4, x5,  [x29, #32]
    ldp     x6, x7,  [sp, #64]
    stp     x6, x7,  [x29, #48]
    ldp     x8, x9,  [sp, #80]
    stp     x8, x9,  [x29, #64]
    ldp     x10, x11, [sp, #96]
    stp     x10, x11, [x29, #80]
    ldp     x12, x13, [sp, #112]
    stp     x12, x13, [x29, #96]
    ldp     x14, x15, [sp, #128]
    stp     x14, x15, [x29, #112]
    ldp     x16, x17, [sp, #144]
    stp     x16, x17, [x29, #128]
    ldp     x18, x19, [sp, #160]
    stp     x18, x19, [x29, #144]
    ldp     x20, x21, [sp, #176]
    stp     x20, x21, [x29, #160]
    ldp     x22, x23, [sp, #192]
    stp     x22, x23, [x29, #176]
    ldp     x24, x25, [sp, #208]
    stp     x24, x25, [x29, #192]
    ldp     x26, x27, [sp, #224]
    stp     x26, x27, [x29, #208]
    ldr     x0, [sp, #240]
    str     x0, [x29, #224]             /* guest x28 */
    mrs     x0, sp_el1
    str     x0, [x29, #248]
    mrs     x0, sp_el0                /* 站十四:guest current 随帧转写 */
    str     x0, [x29, #416]
    mrs     x0, elr_el2
    str     x0, [x29, #256]
    mrs     x0, spsr_el2
    str     x0, [x29, #264]
    ret
