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
    VENTRY guest_sync               /* 站19C:FPEN 陷阱(EC 0x07)入此 */
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
 * 站19C:per-task FP lazy 化——q 阵列存取撤出(站十八C 的全量搬运退役),
 * 改 FPEN 拨杆:新任务==fp_owner 才开,否则关(首条 FP 指令陷阱
 * EC 0x07 走 guest_sync 由 rt_sync_fault 轮转持有权)。非 FP 任务
 * 零帧税;FP 现场驻留硬件,只在持有权轮转时动帧。
 * 切换计数入 rt_fp_stat(fpstat 判据 M<<N 的分母)。
 * 帧已压栈,x9-x12 作临时自由可用;fp_owner/rt_fp_stat 符号契约在
 * rt_fp.c(q[]/fpcr 偏移 = rtos.h 断言,与 asm 无关后契约只在 C)。
 * 站二十:三符号转 per-CPU 数组(rt_cur_arr/rt_fp_owner_arr/
 * rt_fp_stat_arr),TPIDR_EL1 = 本核号(引导/次核入口各写一次)做
 * 索引;x9 全程驻核号。stat 是 u32 数组,索引步长 4。 */
.equ RT_FPEN_MASK, 0x300000
.macro RT_SWITCH_BOOKKEEP
    mrs     x9, tpidr_el1          /* x9 = 本核号(全程驻留) */
    ldr     x1, =rt_cur_arr
    ldr     x2, [x1, x9, lsl #3]   /* x2 = 旧任务(本核 cur) */
    mov     x3, sp
    str     x3, [x2]               /* 旧->sp = 帧顶(sp 偏移 0 契约) */
    ldr     x1, =rt_fp_owner_arr
    ldr     x10, [x1, x9, lsl #3]  /* x10 = 本核 fp_owner(每核各持
                                      * 各的 FP 硬件,站二十) */
    ldr     x1, =rt_fp_stat_arr
    ldr     w12, [x1, x9, lsl #2]
    add     w12, w12, #1
    str     w12, [x1, x9, lsl #2]  /* rt_fp_stat++(per-CPU 槽,双核
                                      * RMW 互踩根除;u32:64 位会踩邻) */
    mrs     x12, cpacr_el1
    cmp     x0, x10                /* 新任务==持有者? */
    b.eq    1f
    bic     x12, x12, #RT_FPEN_MASK
    msr     cpacr_el1, x12         /* 非持权者:FPEN=0,FP 指令入陷阱 */
    b       2f
1:  orr     x12, x12, #RT_FPEN_MASK
    msr     cpacr_el1, x12         /* 持权者回场:FPEN=1,现场原封 */
2:
    ldr     x1, =rt_cur_arr
    str     x0, [x1, x9, lsl #3]   /* rt_cur = 新(本核槽) */
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

/* 站19C:任务语境 sync(current-EL SP_ELx 槽,原 guest_park 让位)。
 * 在用入口=EC 0x07:非持权任务首条 FP 指令落此,rt_sync_fault 轮转
 * 持有权(存旧 owner 帧、装 cur 帧、FPEN 开)后原指令重执行(权限
 * 陷阱不退休,ELR 即重执行点)。不切任务,恒续跑原帧;未知 EC 由
 * rt_sync_fault 自行停车(原 guest_park 语义内移)。帧形与 guest_irq
 * 同构(EL1 语境,ELR/SPSR 都在 EL1 侧)。 */
guest_sync:
    RT_PUSH_GPRS
    mrs     x0, elr_el1
    mrs     x1, spsr_el1
    stp     x0, x1, [sp, #-16]!
    bl      rt_sync_fault
    add     sp, sp, #16             /* 丢本帧 elr/psr(硬件里的还是原值) */
    b       .Lregs

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

/* 站二十:次核引导入口(PSCI CPU_ON 落点,x0=context_id=核号)。
 * CPU_ON 帧置 pstate=0x3C5(EL1h+全掩+SPSel=0→SP_EL0),sp 由本处
 * 自配:RTOS 任务/引导语境恒驻 SP_EL0(帧契约),链接器 .cpu1stack
 * 一页只渡 secondary_main 引导段——首个 rt_sched 切走即换 idle1 帧。
 * TPIDR_EL1 登记 per-CPU 索引(簿记宏契约),此后只读。 */
.global rtos_secondary_entry
rtos_secondary_entry:
    msr     daifset, #0xf
    msr     tpidr_el1, x0
    ldr     x1, =guest_vectors      /* 站二十:次核 VBAR 自设——EL1 向量
                                     * 是 per-CPU 面,主核 start.S 的初
                                     * 设不经此核;不设则首个 tick 中断
                                     * 取指落基地址 0+槽偏移(Linux 次核
                                     * 自配是同律) */
    msr     vbar_el1, x1
    isb
    ldr     x1, =__cpu1_stack_top
    mov     sp, x1
    b       rtos_secondary_main
