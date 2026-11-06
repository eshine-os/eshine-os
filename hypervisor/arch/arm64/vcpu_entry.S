/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
/* vCPU 出入口。偏移契约见 vcpu.h:gprs[31]@0 sp@248 pc@256 pstate@264
 * host_ctx@272(x19..x28@272-344,sp@352,lr@360) sp_el0@416。 */
.section .text

/* ---- 陷阱入口:lower-EL sync 向量槽 b 进来,SPSel=1(sp=SP_EL2=excp_stack)
 * x18-x28 属 guest,禁作 scratch;用 excp_stack 停 x0 完成引导 ---- */
.global el1_trap
el1_trap:
    str     x0, [sp, #-16]!
    /* 站十八D:per-CPU cur_vcpu(数组按 mpidr.aff0 索引)。x1 押栈
     * 绕一圈:此时 guest x1 尚未入帧,不许直接拿来当 scratch——
     * 首版教训:直接 mrs x1 会把 guest x1 砸成 cpu 号再进帧,MMIO
     * 的 SRT=1 读 gprs[1] 恒得 1(vm2 全线 \x01、vm0 全哑)。 */
    str     x1, [sp, #-16]!
    mrs     x1, mpidr_el1
    ubfx    x1, x1, #0, #8
    adrp    x0, cur_vcpus
    add     x0, x0, :lo12:cur_vcpus
    ldr     x0, [x0, x1, lsl #3]
    ldr     x1, [sp], #16          /* guest x1 原值回填 */
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
    mrs     x1, sp_el0                /* 站十四:guest current,逐陷阱入帧 */
    str     x1, [x0, #416]
    mrs     x1, tpidr_el1             /* 站68C-8:guest 核号随帧走——EL2 从不
                                       * 存还则物理 TPIDR 残留跨核互染(实测
                                       * core0 语境读到 1:蹦床槽/簿记索引全按
                                       * core1 算=幽灵链出生) */
    str     x1, [x0, #432]
    mrs     x1, elr_el2
    str     x1, [x0, #256]
    mrs     x1, spsr_el2
    str     x1, [x0, #264]
    /* SP_EL2 归位本核栈顶:handler 链不退栈,eret/retire 都不还
     * SP_EL2。不归位则每次陷阱净泄一截,累计碾穿栈底踩进相邻 BSS
     * (实测第 79 发)。站十八D:excp_stacks[cpu](每核一页)。 */
    mrs     x1, mpidr_el1
    ubfx    x1, x1, #0, #8
    adrp    x9, excp_stacks
    add     x9, x9, :lo12:excp_stacks
    add     x9, x9, x1, lsl #12      /* cpu * 0x1000 */
    add     x9, x9, #0x1000          /* 本核栈顶(512 * 8) */
    mov     sp, x9                   /* SPSel=1 -> SP_EL2 */
    /* 站68C-9:同步陷阱存的蹦床窗检查——guest 蹦床区绝无合法
     * SP_EL1 流通值,命中=毒值入 vcpu 帧的当场(核/vcpu/值)。
     * x28 帧后恒死作 vcpu 暂存(bl 坏 x0-x18)。 */
    ldr     x9, [x0, #248]
    movz    x10, #0x4018, lsl #16
    movk    x10, #0x8e00
    cmp     x9, x10
    b.lo    8f
    movz    x10, #0x4018, lsl #16
    movk    x10, #0x9180
    cmp     x9, x10
    b.hs    8f
    mov     x28, x0
    mov     x1, x9
    mov     x2, #1
    bl      hv_sp_el1_tramp_hit
    mov     x0, x28
8:
    b       el1_sync                  /* x0 = vcpu;此路径不再返回 */

/* ---- 续跑:帧即真相 -> 加载 -> eret(noreturn) ---- */
.global trap_resume
trap_resume:                          /* x0 = vcpu */
    ldr     x1, [x0, #248]
    msr     sp_el1, x1
    /* 站68C-9:还原点蹦床窗检查——毒帧被 eret 回 guest 前拦截
     * (tag=3),与两存(tag=1/2)合围注入-消费全链。 */
    movz    x9, #0x4018, lsl #16
    movk    x9, #0x8e00
    cmp     x1, x9
    b.lo    7f
    movz    x9, #0x4018, lsl #16
    movk    x9, #0x9180
    cmp     x1, x9
    b.hs    7f
    mov     x28, x0
    mov     x2, #3
    bl      hv_sp_el1_tramp_hit
    mov     x0, x28
7:
    ldr     x1, [x0, #416]             /* 站十四:还原 guest current */
    msr     sp_el0, x1
    ldr     x1, [x0, #432]             /* 站68C-8:guest TPIDR_EL1 随帧
                                       * 逐陷阱存还(站68F 定谳:TPIDR_EL1
                                       * 是 guest 自己的寄存器——RTOS 拿它
                                       * 存核号,Linux 拿它存 per-CPU 基址,
                                       * 值域归客,宿主只存还不解释) */
    msr     tpidr_el1, x1
    /* 站68F 翻案:68C-13 的"核号钳位"(还原 v->idx 而非存值)撤除——
     * 钳位是幽灵时代"整段被毒"误诊的产物(root-d/e/f 三案证明毒
     * 从不入寄存器,入的是帧/队列),而它对 Linux 客是灾难:
     * Linux 的 TPIDR_EL1=per-CPU 基址(开机写一次),钳位每次
     * eret 把它强写成 vcpu 序号 0/1 → 首个 per-CPU 访问
     * (__rmqueue_pcplist 页池)以 0 为基址 → NULL 解引用于 0,
     * kmem_cache_init 期 Kernel panic(用户 vm2 实测)。
     * vm0 RTOS 恰以核号为 tpidr 故钳位在其上无感=两客一明一暗。
     * idx 字段与断言保留(构造期权威核号,vcpu 检索/帧身份用)。 */
    ldr     x1, [x0, #256]
    msr     elr_el2, x1
    ldr     x1, [x0, #264]
    msr     spsr_el2, x1
    /* x1 不随对装载:x0 的 eret 尾要用"自加载"(x0 兼任地址/目标),
     * 若仍以 x1 中转 x0,会把 gprs[0] 灌回 x1 —— 第六站 MMIO 回填
     * 任意 SRT,SRT=1 时 guest x1 被 x0 的赎金覆盖(HVC 时代只回 x0,
     * 三站未引爆)。x1 与 x0 同批最后回填,互不搭桥。 */
    ldr     x2, [x0, #16]
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
    ldr     x1, [x0, #8]              /* guest x1(先于 x0) */
    ldr     x0, [x0]                  /* guest x0 最后:自加载,免中转 */
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
    mov     sp, x16                   /* 宿主 C 世界同驻 SP_EL2,免切 */
    ret     x17
