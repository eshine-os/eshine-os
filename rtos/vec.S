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
    /* Lower EL AArch64:站二十八 EL0 用户任务(sync=SVC+EL0 缺页,
     * irq=tick/RX 抢占);fiq/serror 与 AArch32 维持终态 park */
    VENTRY guest_el0_sync
    VENTRY guest_el0_irq
    VENTRY guest_park
    VENTRY guest_park
    VENTRY guest_park
    VENTRY guest_park
    VENTRY guest_park
    VENTRY guest_park

guest_park:
    /* 站28 落点加固:宿主泵停泊双重推进陈疾可能以 +8 复活,
     * +4/+8 皆须落受控指令(wfi+8 曾直落 guest_irq 体=无异常
     * 而行压栈序列,nop 垫使 +8 滑回本环) */
    wfi
    nop
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
    /* 站68C:簿记域检——将存入 old->sp 的值(x3)必须落在 old 的
     * 内核栈域 [ksp-4096, ksp)。越域=交叉存/蹦床帧实锤,走
     * .Lbk_viol 桩换引导栈调 C 打印器(x4=adr 取本展开点,三展开
     * 各唯一)。x4/x11/x12 帧已压栈皆死寄存器;ksp=0(idle 伪任务
     * 未设)放行。 */
    ldr     x11, [x2, #RT_KSTK_OFF]
    cbz     x11, 9f
    sub     x12, x11, #4096
    cmp     x3, x12
    adr     x4, 9f
    b.lo    .Lbk_viol
    cmp     x3, x11
    b.hs    .Lbk_viol
9:  str     x3, [x2]               /* 旧->sp = 帧顶(sp 偏移 0 契约) */
    /* 站35:TPIDR_EL0 随任务走(用户 TLS 契约,musl 主线程自写、
     * pthread 线程=clone SETTLS 初值;EL1 可读写 EL0 寄存器)。
     * 存还配对同帧手术纪律:@736 亦在 272B 帧外,不清点即失明。 */
    mrs     x3, tpidr_el0
    str     x3, [x2, #736]
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
    ldr     x3, [x0, #736]
    msr     tpidr_el0, x3
    /* 站29:地址空间切换点(参考实现 switch_mm 的调度器内联形)。任务
     * ttbr(asid<<48|L0;@696 契约)每切换必装:ASID 标签保 TLB 存活,
     * 免逐切换冲刷;0=内核 PGD(EL0 永不上内核表——.user/mmap 槽
     * 在 mm0 置 0,物理隔离由 per-process 表承担)。x3 此处已死
     * (sp 已取走其值),复用免加 scratch。 */
    ldr     x3, [x0, #696]
    cbnz    x3, 3f
    ldr     x1, =rt_mm0_l0          /* 内核 PGD(双间接) */
    ldr     x3, [x1]
3:
    msr     ttbr0_el1, x3
    isb
.endm

/* 站68C:簿记域检违例桩(三展开点共用)。x2=old 任务,x3=毒 sp,
 * x11=ksp,x4=案发展开点,x0=next(簿记的切换目标)。当前 sp=毒值
 * (蹦床域)不可用,换引导栈调 C 打印器;noreturn(打印+park)。
 * 站68C-2 全现场捕获:x4-x7 承被中断上下文四元(elr/spsr/next/
 * sp_el1)——sp2[0]=SPSel 位+M[3:2]=特权级,直读"谁在 EL1SPSel=1
 * 且 SP_EL1=蹦床残值"这一原罪语境;elr 对 map 即函数名。 */
.Lbk_viol:
    mov     x9, x4                 /* site 先保(x9 帧后恒死) */
    mrs     x7, sp_el1             /* 案发库现值 */
    mrs     x5, spsr_el1           /* 被中断 PSTATE */
    mov     x6, x0                 /* next=切换目标(先于 x0 覆写) */
    mov     x1, x3                 /* 毒 sp */
    mov     x0, x2                 /* old */
    mrs     x4, elr_el1            /* 被中断 PC */
    mov     x3, x9                 /* site */
    mov     x2, x11                /* ksp */
    ldr     x12, =__stack_top
    mov     sp, x12
    bl      rt_bookkeep_violation

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
    /* 站二十八:x5/x6 承 elr/psr(吞了 x0=新任务指针就取不到 usp);
     * psr[3:2] 分流目标特权级,双库不变量在此归位:
     *   EL0 目标:usp→sp_el0,弹栈改经 SP_EL0 库(task->sp 同址,
     *     两语境通用),SP_EL1:=本核蹦床顶——eret→EL0 的硬约定
     *     (下次 EL0 异常入口硬件 sp=SP_EL1,stp 必落 32B 蹦床域)
     *   EL1h 目标:eret 后 SPSel=1、sp=SP_EL1 必须是任务自己栈。
     *     SPSel=1 语境(站16-27 原路径)簿记 mov sp 已写对,零改动;
     *     SPSel=0 语境(自 EL0 异常处理切来)须换库补写。
     * 事实基准:本 RTOS 全帧契约 EL1h(psr=5/0x3C5 三处铁证,
     * 站16 帧自铸即此),EL1h 任务的活栈驻 SP_EL1——蹦床与它
     * 同库不同值,故凡离开 EL0 必重臂、凡回 EL1h 必归还。 */
    ldp     x5, x6, [sp], #16       /* 目标帧 elr/psr */
    msr     elr_el1, x5
    msr     spsr_el1, x6
    tst     x6, #0xc
    bne     .Lel1
    /* EL0 目标:usp/蹦床顶先藏进已消费的 elr/psr 槽([sp-16/-8]),
     * 弹完 31 寄存器后无 scratch,自藏点取回再整库——EL0 的 sp=
     * SP_EL0 寄存器,必须弹栈之后置 usp(曾先置 usp 再借 sp 弹栈,
     * SP_EL0 被帧顶覆写,壳以内核栈入场,首压栈即越权→诚实终态,
     * 冒烟定谳);SP_EL1 同刻复位蹦床(eret→EL0 不变量) */
    ldr     x5, [x0, #672]          /* task->usp(偏移断言在 rtos.h) */
    mrs     x6, tpidr_el1
    ldr     x7, =rt_el0_tramp       /* 32B/核:lsl #5 索引 */
    add     x7, x7, x6, lsl #5
    add     x7, x7, #32
    str     x5, [sp, #-16]          /* usp → 旧 elr 槽 */
    str     x7, [sp, #-8]           /* 蹦床顶 → 旧 psr 槽 */
    RT_POP_GPRS                     /* +256:此后 sp=帧基+256 */
    sub     sp, sp, #272            /* 移回帧基-16(藏点;弹后无人用 sp) */
    ldr     x5, [sp]                /* 取回 usp */
    ldr     x6, [sp, #8]            /* 取回蹦床顶 */
    msr     spsel, #0               /* 归 SP_EL0 库:mov sp 即写用户栈
                                     * (banked SP 直访 EL1 UNDEF,冒烟定谳) */
    mov     sp, x5                  /* EL0 的 sp=usp */
    msr     spsel, #1
    mov     sp, x6                  /* SP_EL1 := 蹦床顶 */
    msr     spsel, #0               /* sp 回 SP_EL0=usp(eret 面) */
    eret
.Lel1:
    mrs     x5, spsel
    cbnz    x5, .Lregs             /* SPSel=1:簿记 mov sp 已写 SP_EL1,
                                     * .Lpop 顶 ldp 亦已推进本库,零改动 */
    /* SPSel=0 语境(自 EL0 异常处理链切来):.Lpop 顶 ldp 已把当前库
     * (SP_EL0)推到 task->sp+16 = GPR 块基(bank 无关)。弹栈必须在
     * 目标的 SP_EL1 库就地进行:换库前抓现值,换库后原地弹到底——
     * 弹栈的推进即归 SP_EL1,eret 后 EL1h 的 sp=task->sp+272 全量
     * 回卷。曾以帧基 [x0] 归位后切回 SP_EL0 库弹:推进全记在
     * SP_EL0,eret 后 SP_EL1 仍是未弹旧顶——每轮自 EL0 语境向
     * EL1h 派发净漏 272B,idle 引导栈 ~20 周期踩穿(8KB),tick 帧
     * (272B)落进 .bss tp[] 软定时器表,"回复在而 tick 冻结"全案
     * 即此(站28 vtimer 专站:gdb 棘轮直方图+差分三采样定谳)。
     * 修正版曾漏 +16(帧基 ≠ GPR 块基):弹栈整体左移一字,寄存器
     * 全串位,heartbeat elr=0x0 IABT 即时定谳——故用抓现值习语
     * (同 RT_EL0_RET mov x2,sp),消灭重载+补偏的重复记账 */
    mov     x5, sp                 /* GPR 块基 = task->sp+16(换库前) */
    msr     spsel, #1
    mov     sp, x5                 /* SP_EL1 := GPR 块基,留在本库弹 */
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

/* ================= 站二十八:EL0 异常入口(lower-EL 槽) =================
 *
 * 硬件事实:EL0 异常进入 EL1 时 SPSel 强制置 1(sp=SP_EL1),而
 * SP_EL0 仍持用户栈。本 RTOS 帧契约是 EL1h(SPSel=1,活栈驻
 * SP_EL1——rt_frame_init psr=5/rt_switch_to orr#5/PSCI 0x3C5),
 * 但 EL0 异常入口的 SP_EL1 必须是蹦床(不变量由 .Lpop EL0 路径
 * 与 RT_EL0_RET 维护:凡 eret→EL0,SP_EL1:=本核蹦床顶)。
 * 归一舞蹈:三 scratch(x9-x11)先存 SP_EL1 蹦床(rt_cur 还没找着,
 * 系统里唯一可用寄存器域);找 rt_cur 存 task->usp;切 SPSel=0,
 * sp 由 SP_EL0(=用户栈)换 task->ksp 内核栈——SP_EL0 寄存器自此
 * 充 EL0 处理语境的帧库;RT_PUSH_GPRS 后把蹦床上的用户 x9/x10/x11
 * 回填帧槽(IRQ 语义要求恢复;SVC 本可豁免 caller-saved,统一走
 * 稳妥)并复位 SP_EL1 供下次。此后全套帧机器(阻塞/续跑/切换)
 * 零改动:
 * - SVC 处理中 rt_sched 阻塞 → rt_switch_to 在内核栈上压协作帧
 *   (老帧=用户上下文完整驻栈底)→ 醒来继续,eret 落回用户;
 * - EL0 IRQ + 切走:用户帧即 old->sp(elr=EL0 续跑点),.Lpop 的
 *   EL0 觉察恢复 usp——两路在既有 .Lpop/.Lcoop_ret 上闭环。 */
.equ RT_USTK_OFF, 672
.equ RT_KSTK_OFF, 680

.macro RT_EL0_ENTER
    stp     x9, x10, [sp, #-16]!   /* 蹦床域存 scratch(基址=tramp 顶) */
    str     x11, [sp, #-16]!
    mrs     x9, tpidr_el1
    ldr     x10, =rt_cur_arr
    ldr     x10, [x10, x9, lsl #3] /* x10 = rt_cur */
    msr     spsel, #0              /* 用户栈经当前库读:banked SP 直访
                                   * EL1 UNDEF(冒烟定谳,同 sp_el1 律) */
    mov     x11, sp
    str     x11, [x10, #RT_USTK_OFF]
    ldr     x11, [x10, #RT_KSTK_OFF]
    mov     sp, x11                /* 内核帧栈(kstack 顶) */
    RT_PUSH_GPRS                   /* x9/x10/x11 槽此刻是 scratch 值 */
    mrs     x0, elr_el1
    mrs     x1, spsr_el1
    stp     x0, x1, [sp, #-16]!
    /* 回填用户 x9/x10/x11(蹦床布局:+0=x11,+16=x9,+24=x10)。
     * 帧槽偏移(帧基=elr 槽):x0@+16 带 8B hole,x_i 紧排于
     * 24+8i——x9@+96/x10@+104/x11@+112(站28 冒烟定谳:曾按
     * 16B 步长错写 160/176/192,即 x19/x21/x23 槽,跨调用 ABI
     * 寄存器遭偷换) */
    mrs     x9, tpidr_el1
    ldr     x10, =rt_el0_tramp
    add     x10, x10, x9, lsl #5
    ldr     x11, [x10, #16]
    str     x11, [sp, #96]         /* 帧 x9 槽(24+8*9) */
    ldr     x11, [x10, #24]
    str     x11, [sp, #104]        /* 帧 x10 槽 */
    ldr     x11, [x10, #0]
    str     x11, [sp, #112]        /* 帧 x11 槽 */
    add     x11, x10, #32
    /* 复位 SP_EL1 经 spsel 换库:直写 msr sp_el1 在 EL1 落 UNDEF
     * (站 28 冒烟 -d int 定谳,EC=0);换库瞬间 sp 指向 SP_EL1 旧值,
     * mov sp 后即回 SPsel=0 内核栈帧,DAIF 已掩无中断窗 */
    msr     spsel, #1
    mov     sp, x11
    msr     spsel, #0
.endm

.macro RT_EL0_RET
    /* 站35 定谳续:本尾巴自 SVC 开中断语境(vec.S 站28 daifclr)
     * 抵达,弹栈机(elr/spsr 装库+级联+usp)全程裸奔——tick 落
     * 窗即嵌合帧(同 rt_switch_to 定谳,panic 三数 esr=0x96000006
     * elr=尾巴PC far=0x2a0 与无声楔两脸同根)。掩码与 eret 不冲:
     * eret 携帧 psr(用户 EL0t 开中断态)自动回开,fault 路本即
     * 硬件掩,重复掩为幂等 no-op。 */
    msr     daifset, #0xf
    ldp     x0, x1, [sp], #16      /* elr/psr(硬件现值覆写回帧语义) */
    msr     elr_el1, x0
    msr     spsr_el1, x1
    /* 先抓 GPR 块地址(bank 无关):阻塞唤醒路径以 SPSel=1 抵此
     * (coop 帧 eret EL1h),SP_EL0 库驻陈旧 coop 顶——裸舞后弹栈
     * 曾弹出 C 语境寄存器(x20=0,strb 直落 far=0 定谳),故舞后
     * 须显式归还帧址;同任务路(SPSel=0)此归还为幂等 no-op */
    mov     x2, sp                  /* GPR 块 = 帧基+16 */
    mrs     x0, tpidr_el1
    ldr     x1, =rt_el0_tramp
    add     x1, x1, x0, lsl #5
    add     x1, x1, #32
    msr     spsel, #1
    mov     sp, x1                  /* SP_EL1 := 蹦床顶(不变量) */
    msr     spsel, #0
    mov     sp, x2                  /* 弹栈归 GPR 块(两库同址) */
    RT_POP_GPRS
    mrs     x9, tpidr_el1
    ldr     x10, =rt_cur_arr
    ldr     x10, [x10, x9, lsl #3]
    ldr     x11, [x10, #RT_USTK_OFF]
    mov     sp, x11                /* user SP 归位:SPSel=0 下 mov 即写
                                     * SP_EL0(banked 直访 EL1 UNDEF) */
    eret
.endm

guest_el0_sync:
    RT_EL0_ENTER
    mrs     x0, esr_el1
    lsr     x0, x0, #26
    cmp     x0, #0x15              /* EC == SVC(AArch64)? */
    b.ne    1f
    /* 站28 vtimer 专站定谳:SVC 处理语境解锁 DAIF。阻塞系统调用
     * (read 之 block_rx、demo 之 sleep)会把本语境挂到空闲 wfi——
     * 硬件异常入口的全掩 DAIF 若不解除,泊位 pstate 即存成掩码:
     * 唤醒后 LR 注入的虚拟 tick 永递不进(PSTATE.I 拦),cval 冻结
     * → CARM-STALL 绊线停醒 = "回复在而 tick 冻结"(wake_retries
     * 实测 89576、psr=0x3c5、ctl ISTATUS=1 三证齐)。站16-27 任务
     * 语境恒非掩(EL1h 壳阻塞即此形),本行 = 该契约在 syscall
     * 边界的延续;dispatch 内阻塞/锁机器本按可抢占建成。 */
    msr     daifclr, #0xf
    mov     x0, sp
    bl      rt_syscall_dispatch    /* 返回值已写帧 x0 槽 */
    RT_EL0_RET
1:  mov     x0, sp
    bl      rt_el0_fault           /* 站29:翻译错经 rt_pfault 补页后
                                     * 正常返回(帧未动,原地重执行);
                                     * VMA 外/其余 FSC=诚实终态不归 */

    RT_EL0_RET

guest_el0_irq:
    RT_EL0_ENTER
    bl      rt_irq_dispatch        /* x0:0=续跑原任务,否则新任务指针 */
    cbz     x0, .Lel0_same
    RT_SWITCH_BOOKKEEP             /* 用户帧即 old->sp(elr=EL0 续跑点) */
    b       .Lpop
.Lel0_same:
    RT_EL0_RET

/* EL0 任务首帧:elr=entry 直达(psr=EL0t,DAIF 开),x0=arg;帧落
 * kstack(内核域);usp 初值=u栈顶。entry 不返回(exit 走 sys_hvc
 * 内核收尸)——x30 槽留零,若返回即落 0 取指=EL0 缺页→诚实终态面。 */
.global rt_frame_init_el0
rt_frame_init_el0:                 /* x0=task x1=kstack_top x2=ustack_top
                                     * x3=entry x4=arg */
    sub     x1, x1, #272
    str     x3, [x1, #0]            /* elr = entry */
    str     xzr, [x1, #8]           /* psr = EL0t */
    str     x4, [x1, #16]           /* x0 槽 = arg */
    str     x1, [x0]                /* task->sp = 内核帧顶 */
    str     x2, [x0, #RT_USTK_OFF]   /* usp */
    add     x1, x1, #272
    str     x1, [x0, #RT_KSTK_OFF]  /* ksp = kstack 顶 */
    ret

/* 主动切换(任务上下文):伪造同款帧,elr=.Lcoop_ret,psr=EL1h+当前
 * DAIF。x0(新任务指针)被 RT_PUSH_GPRS 存进 x0 槽,从 [sp+16] 回捞。 */
.global rt_switch_to
rt_switch_to:
    /* 站35 定谳(三数咬合):本函数经 rt_sched/rt_task_exit 在 SVC
     * 开中断语境(vec.S:342 站28 契约)被调——psr 伪造+簿记+弹栈
     * (.Lpop)全程裸奔。tick 落窗 = 嵌套 IRQ 存嵌合帧(.Lpop 顶
     * msr spsr_el1 已装新任务 EL0 态):不切换路径 eret 把执行流
     * 以 EL0 送回尾巴 PC(x0=刚弹出的新任务 x0;clone 子 x0=0)→
     * ldr [x0,#672]@EL0 → far=0x2a0 panic 脸;切换路径帧半消费
     * (sp=弹栈中途位)→ 弹垃圾→eret 野址→RUN 标记永失 = 无声
     * 楔(pt36 尸检帧像定谳)。站28 前 7 站无雷只因无同优先级 RR
     * 竞争者令 tick 真切走 mid-tail 任务——pt 4 线程首个命中。
     * 帧恢复机器必须不可重入(IRQ 尾巴本即硬件掩)。push 段可被
     * 抢占无害(帧完整+psr=EL1h 非嵌合,续跑即续推);掩码落在
     * push 之后、psr 伪造之前——x1 捕获在 push 后取帧槽无损。
     * 站28 契约不破:泊位 pstate 由 C 侧显式写帧,与此掩无涉;
     * .Lcoop_ret 续跑 eret 携 psr(掩前捕获)自动回开中断态。 */
    RT_PUSH_GPRS
    mrs     x1, daif
    and     x1, x1, #0x3c0          /* 保 DAIF 位 */
    mov     x3, #5
    orr     x1, x1, x3              /* M=0101 EL1h */
    msr     daifset, #0xf           /* 簿记+.Lpop 原子窗(见上定谳) */
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
