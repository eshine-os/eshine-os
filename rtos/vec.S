/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
/* eshine-rtos EL1 向量与切换器(第十六站)。
 *
 * 统一任务帧(272B,帧顶=elr 槽):
 *   [elr][psr][x0 槽(带 8B hole)][x1,x2]…[x29,x30]
 * 帧自描述:elr 即续跑点——IRQ 抢占帧回被打断处,让出帧回
 * rt_switch_to 的 .Lcoop_ret,首启动帧回 rt_trampoline。续跑只有
 * 一条路:.Lpop(装 elr/psr + 弹 31 寄存器 + eret)。C 只做选择
 * (rt_irq_dispatch / rt_sched),asm 做簿记(旧->sp = 帧顶;
 * rt_cur = 新;sp = 新帧顶)。 */
.macro VENTRY target
    .align 7
    b       \target
.endm

.section .vectors, "ax"
.align 11
.global guest_vectors
guest_vectors:
    /* Current EL, SP_EL0 */
    VENTRY guest_park               /* sync */
    VENTRY guest_irq
    VENTRY guest_park               /* fiq */
    VENTRY guest_park               /* serror */
    /* Current EL, SP_ELx(常驻组) */
    VENTRY guest_park               /* sync */
    VENTRY guest_irq
    VENTRY guest_park
    VENTRY guest_park
    /* Lower EL:EL1 终态,永不到达(同 guest/vec.S) */
    VENTRY guest_park
    VENTRY guest_park
    VENTRY guest_park
    VENTRY guest_park
    VENTRY guest_park
    VENTRY guest_park
    VENTRY guest_park
    VENTRY guest_park

guest_park:
    wfi
    b       guest_park

/* 压 31 寄存器(不含 elr/psr——由各入口自定)。推序与 guest/vec.S
 * 同律,故帧内偏移与 RT_FRAME_POP_REGS 严格镜像。 */
.macro RT_PUSH_GPRS
    stp     x29, x30, [sp, #-16]!
    stp     x27, x28, [sp, #-16]!
    stp     x25, x26, [sp, #-16]!
    stp     x23, x24, [sp, #-16]!
    stp     x21, x22, [sp, #-16]!
    stp     x19, x20, [sp, #-16]!
    stp     x17, x18, [sp, #-16]!
    stp     x15, x16, [sp, #-16]!
    stp     x13, x14, [sp, #-16]!
    stp     x11, x12, [sp, #-16]!
    stp     x9,  x10, [sp, #-16]!
    stp     x7,  x8,  [sp, #-16]!
    stp     x5,  x6,  [sp, #-16]!
    stp     x3,  x4,  [sp, #-16]!
    stp     x1,  x2,  [sp, #-16]!
    str     x0,  [sp, #-16]!
.endm

.macro RT_POP_GPRS
    ldr     x0,  [sp], #16
    ldp     x1,  x2,  [sp], #16
    ldp     x3,  x4,  [sp], #16
    ldp     x5,  x6,  [sp], #16
    ldp     x7,  x8,  [sp], #16
    ldp     x9,  x10, [sp], #16
    ldp     x11, x12, [sp], #16
    ldp     x13, x14, [sp], #16
    ldp     x15, x16, [sp], #16
    ldp     x17, x18, [sp], #16
    ldp     x19, x20, [sp], #16
    ldp     x21, x22, [sp], #16
    ldp     x23, x24, [sp], #16
    ldp     x25, x26, [sp], #16
    ldp     x27, x28, [sp], #16
    ldp     x29, x30, [sp], #16
.endm

/* 簿记:x0=新任务(C 已选好);保存旧任务帧顶,切 sp 与 rt_cur。
 * SP 不能直访内存,经 x3 中转。
 * 站十八C:per-task FP 帧存取内嵌——两个切换点(rt_switch_to 与
 * guest_irq)共用本宏,一处扩展双点生效。整数帧已压栈,x9/x10 作
 * 临时自由;q 寄存器操作不碰整数现场;偏移契约 = rtos.h 断言。 */
.equ RT_TASK_Q,    96
.equ RT_TASK_FPCR, 608
.macro RT_SWITCH_BOOKKEEP
    ldr     x1, =rt_cur
    ldr     x2, [x1]               /* x2 = 旧任务 */
    mov     x3, sp
    str     x3, [x2]               /* 旧->sp = 帧顶(sp 偏移 0 契约) */
    stp     q0,  q1,  [x2, #(RT_TASK_Q+0)]
    stp     q2,  q3,  [x2, #(RT_TASK_Q+32)]
    stp     q4,  q5,  [x2, #(RT_TASK_Q+64)]
    stp     q6,  q7,  [x2, #(RT_TASK_Q+96)]
    stp     q8,  q9,  [x2, #(RT_TASK_Q+128)]
    stp     q10, q11, [x2, #(RT_TASK_Q+160)]
    stp     q12, q13, [x2, #(RT_TASK_Q+192)]
    stp     q14, q15, [x2, #(RT_TASK_Q+224)]
    stp     q16, q17, [x2, #(RT_TASK_Q+256)]
    stp     q18, q19, [x2, #(RT_TASK_Q+288)]
    stp     q20, q21, [x2, #(RT_TASK_Q+320)]
    stp     q22, q23, [x2, #(RT_TASK_Q+352)]
    stp     q24, q25, [x2, #(RT_TASK_Q+384)]
    stp     q26, q27, [x2, #(RT_TASK_Q+416)]
    stp     q28, q29, [x2, #(RT_TASK_Q+448)]
    stp     q30, q31, [x2, #(RT_TASK_Q+480)]
    mrs     x9, fpcr
    mrs     x10, fpsr
    str     x9, [x2, #RT_TASK_FPCR]      /* 64 位 stp 量程 ±504,608 用两条 */
    str     x10, [x2, #(RT_TASK_FPCR+8)]
    str     x0, [x1]               /* rt_cur = 新 */
    ldp     q0,  q1,  [x0, #(RT_TASK_Q+0)]
    ldp     q2,  q3,  [x0, #(RT_TASK_Q+32)]
    ldp     q4,  q5,  [x0, #(RT_TASK_Q+64)]
    ldp     q6,  q7,  [x0, #(RT_TASK_Q+96)]
    ldp     q8,  q9,  [x0, #(RT_TASK_Q+128)]
    ldp     q10, q11, [x0, #(RT_TASK_Q+160)]
    ldp     q12, q13, [x0, #(RT_TASK_Q+192)]
    ldp     q14, q15, [x0, #(RT_TASK_Q+224)]
    ldp     q16, q17, [x0, #(RT_TASK_Q+256)]
    ldp     q18, q19, [x0, #(RT_TASK_Q+288)]
    ldp     q20, q21, [x0, #(RT_TASK_Q+320)]
    ldp     q22, q23, [x0, #(RT_TASK_Q+352)]
    ldp     q24, q25, [x0, #(RT_TASK_Q+384)]
    ldp     q26, q27, [x0, #(RT_TASK_Q+416)]
    ldp     q28, q29, [x0, #(RT_TASK_Q+448)]
    ldp     q30, q31, [x0, #(RT_TASK_Q+480)]
    ldr     x9, [x0, #RT_TASK_FPCR]
    ldr     x10, [x0, #(RT_TASK_FPCR+8)]
    msr     fpcr, x9
    msr     fpsr, x10
    ldr     x3, [x0]
    mov     sp, x3                 /* sp = 新帧顶 */
.endm

guest_irq:
    RT_PUSH_GPRS
    mrs     x0, elr_el1
    mrs     x1, spsr_el1
    stp     x0, x1, [sp, #-16]!
    bl      rt_irq_dispatch        /* x0:0=续跑原任务,否则新任务指针 */
    cbz     x0, .Lpop_same
    RT_SWITCH_BOOKKEEP
    b       .Lpop
.Lpop_same:
    add     sp, sp, #16             /* 丢本帧 elr/psr(硬件里的还是原值) */
    b       .Lregs
.Lpop:
    ldp     x0, x1, [sp], #16       /* 目标帧 elr/psr */
    msr     elr_el1, x0
    msr     spsr_el1, x1
.Lregs:
    RT_POP_GPRS
    eret

/* 主动切换(任务上下文):伪造同款帧,elr=.Lcoop_ret,psr=EL1h+当前
 * DAIF。x0(新任务指针)被 RT_PUSH_GPRS 存进 x0 槽,从 [sp+16] 回捞。 */
.global rt_switch_to
rt_switch_to:
    RT_PUSH_GPRS
    mrs     x1, daif
    and     x1, x1, #0x3c0          /* 保 DAIF 位 */
    mov     x3, #5
    orr     x1, x1, x3              /* M=0101 EL1h */
    adr     x2, .Lcoop_ret
    stp     x2, x1, [sp, #-16]!     /* 帧顶 = 本任务续跑帧(elr 自描述) */
    ldr     x0, [sp, #16]           /* 回捞入参:新任务指针 */
    RT_SWITCH_BOOKKEEP
    b       .Lpop
.Lcoop_ret:
    ret

/* 新任务首帧伪造:帧同构,elr=rt_trampoline,x0=arg,x1=entry。
 * 其余槽位留给静态阵列的零值(x1 槽在 +32,x0 槽后带 8B hole)。 */
.global rt_frame_init
rt_frame_init:                      /* x0=task x1=stack_top x2=entry x3=arg */
    sub     x1, x1, #272
    adr     x4, rt_trampoline
    str     x4, [x1, #0]            /* elr */
    mov     x4, #5
    str     x4, [x1, #8]            /* psr:EL1h,DAIF 开 */
    str     x3, [x1, #16]           /* x0 槽 = arg */
    str     x2, [x1, #32]           /* x1 槽 = entry */
    str     x1, [x0]                /* task->sp = 帧顶 */
    ret

rt_trampoline:                      /* 帧里:x0=arg,x1=entry */
    blr     x1
    b       rt_task_exit            /* entry 返回即自杀切换,noreturn */
