/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "rtos.h"

/* FP 自证(第十六站判据):d0 累加 1..N 的 double 和(N=1e6,部分和
 * 与终值皆整数 < 2^53,double 上精确)。全程手写 FP 指令——仓级
 * -mgeneral-regs-only 下编译器永不染指 FP,防常量折叠天然成立;
 * Q 现场跨任务切换存活(他任务 FP-free)与跨客切换存活(宿主
 * per-VM SIMD 帧)就是本判据的靶心。 */

static u64 fp_sum_check(u64 n, u64 want)
{
    u64 got;

    __asm__ volatile(
        "fmov  d0, xzr\n\t"          /* acc = 0.0 */
        "mov   x9, #1\n\t"
        "1:\n\t"
        "scvtf d1, x9\n\t"           /* d1 = (double)x9 */
        "fadd  d0, d0, d1\n\t"
        "add   x9, x9, #1\n\t"
        "cmp   x9, %1\n\t"
        "b.lt  1b\n\t"
        "fcvtzu %0, d0\n\t"
        : "=r"(got)
        : "r"(n + 1)
        : "v0", "v1", "x9", "cc");
    return got == want;
}

void rt_fp_task(void *arg)
{
    (void)arg;
    if (fp_sum_check(1000000ULL, 500000500000ULL))
        rt_puts("fp OK: 500000500000\n");
    else
        rt_puts("fp FAIL: fp state corrupted\n");
}

/* 站十八C:fpdual 双 FP 任务并发自证——A 印独立针(复用 Σi 闭式),
 * B 累加 Σ(i×0.5)(半值闭式:每项与部分和皆精确表示 <2^53)。
 * 两任务跨 tick 抢占交错,双结果皆精确 = per-task FP 帧无损;
 * 帧不保则先跑者的累加器被后者清零,必炸。 */

/* ---- 站十九C:per-task FP lazy 化 ----
 * fp_owner 单一持有:硬件 FP 现场永远只属于一个任务;切换只拨
 * CPACR_EL1.FPEN(簿记),不搬帧。非持权任务首条 FP 指令陷阱
 * EC 0x07 走 guest_sync → rt_sync_fault 轮转:存旧 owner 入其
 * 帧(q[]/fpcr/fpsr,纯整数语境的 C 拷贝+显式 asm 存取控制寄存
 * 器),装新 owner 帧,FPEN 开,原指令重执行。非 FP 任务零帧税。
 * 计数:rt_fp_stat=切换次数(簿记 asm ++,分母)、rt_fp_saves=
 * 持有权轮转数(分子,fpstat 判据 M<<N)。符号名与 vec.S 的
 * ldr =rt_fp_owner/=rt_fp_stat 契约。 */
rt_task_t *rt_fp_owner_arr[RT_NCPU];    /* 站二十:per-CPU 持有者(bss 零=
                                         * 无持有者;每核各持各的 FP 硬件,
                                         * vec.S 簿记宏按 TPIDR 索引) */
u32 rt_fp_stat_arr[RT_NCPU];            /* 站二十:per-CPU 切换计数(簿记
                                         * asm;RT_NCPU=1 时退化单槽) */
u32 rt_fp_saves;                        /* 持有权轮转计数 */

u32 rt_fp_stat_total(void)              /* 站二十:fpstat 显示面 = 两核求和 */
{
    return rt_fp_stat_arr[0] + rt_fp_stat_arr[1];
}

/* 偏移即 rtos.h 断言的帧契约(q@96/fpcr@608/fpsr@616);此处直书常量,
 * 断言变动即编译期炸,两处不会静默漂移。整仓 -mgeneral-regs-only:
 * 编译器永不分配 v 寄存器,显式 asm 成对 stp/ldp 存取即全部真相。 */
static void fp_frame_save(rt_task_t *t)
{
    __asm__ volatile(
        "stp q0,  q1,  [%0, #(96+0)]\n\t"
        "stp q2,  q3,  [%0, #(96+32)]\n\t"
        "stp q4,  q5,  [%0, #(96+64)]\n\t"
        "stp q6,  q7,  [%0, #(96+96)]\n\t"
        "stp q8,  q9,  [%0, #(96+128)]\n\t"
        "stp q10, q11, [%0, #(96+160)]\n\t"
        "stp q12, q13, [%0, #(96+192)]\n\t"
        "stp q14, q15, [%0, #(96+224)]\n\t"
        "stp q16, q17, [%0, #(96+256)]\n\t"
        "stp q18, q19, [%0, #(96+288)]\n\t"
        "stp q20, q21, [%0, #(96+320)]\n\t"
        "stp q22, q23, [%0, #(96+352)]\n\t"
        "stp q24, q25, [%0, #(96+384)]\n\t"
        "stp q26, q27, [%0, #(96+416)]\n\t"
        "stp q28, q29, [%0, #(96+448)]\n\t"
        "stp q30, q31, [%0, #(96+480)]\n\t"
        "mrs x9, fpcr\n\t"
        "mrs x10, fpsr\n\t"
        "str x9, [%0, #608]\n\t"
        "str x10, [%0, #616]"
        :: "r"(t) : "x9", "x10", "memory");
}

static void fp_frame_load(rt_task_t *t)
{
    __asm__ volatile(
        "ldp q0,  q1,  [%0, #(96+0)]\n\t"
        "ldp q2,  q3,  [%0, #(96+32)]\n\t"
        "ldp q4,  q5,  [%0, #(96+64)]\n\t"
        "ldp q6,  q7,  [%0, #(96+96)]\n\t"
        "ldp q8,  q9,  [%0, #(96+128)]\n\t"
        "ldp q10, q11, [%0, #(96+160)]\n\t"
        "ldp q12, q13, [%0, #(96+192)]\n\t"
        "ldp q14, q15, [%0, #(96+224)]\n\t"
        "ldp q16, q17, [%0, #(96+256)]\n\t"
        "ldp q18, q19, [%0, #(96+288)]\n\t"
        "ldp q20, q21, [%0, #(96+320)]\n\t"
        "ldp q22, q23, [%0, #(96+352)]\n\t"
        "ldp q24, q25, [%0, #(96+384)]\n\t"
        "ldp q26, q27, [%0, #(96+416)]\n\t"
        "ldp q28, q29, [%0, #(96+448)]\n\t"
        "ldp q30, q31, [%0, #(96+480)]\n\t"
        "ldr x9, [%0, #608]\n\t"
        "ldr x10, [%0, #616]\n\t"
        "msr fpcr, x9\n\t"
        "msr fpsr, x10"
        :: "r"(t) : "x9", "x10", "memory");
}

/* 站19C:guest_sync 入口(vec.S bl)。EC 0x07=CPACR FPEN 陷阱:
 * 活 FP 现场此刻仍属旧持有者(本任务首条 FP 指令未执行),存旧装新
 * 开 FPEN,返回后原指令重执行。未知 EC 停车(原 guest_park 语义)。 */
/* 站33:FP 陷阱体抽出——EL1 guest_sync 与 EL0 guest_el0_sync 共用
 * (musl memcpy/printf 的 NEON 首触在 EL0,旧形只挂 EL1 向量,
 * EL0 sync 落终态面 panic,冒烟定谳 esr EC 0x07 elr=镜像内)。
 * 调用者已判 EC==0x07;返回后原指令重执行。 */
void rt_fp_trap_cur(void)
{
    rt_task_t *cur = rt_cur;
    rt_task_t *old;

    /* FPEN 必须先于任何帧指令(q 存取自身就是 FP 指令,关着执行
     * = 自陷阱递归炸栈——探针实锤,cur/owner 恒初值)。 */
    __asm__ volatile(
        "mrs x9, cpacr_el1\n\t"
        "orr x9, x9, #0x300000\n\t"
        "msr cpacr_el1, x9\n\t"
        "isb"
        ::: "x9", "memory");
    old = rt_fp_owner;
    if (old && old != cur) {
        fp_frame_save(old);            /* 现场此刻仍属旧持有者 */
        rt_fp_saves++;
    }
    if (old != cur) {
        fp_frame_load(cur);
        rt_fp_owner = cur;
    }
}

void rt_sync_fault(void)
{
    u64 esr;

    __asm__ volatile("mrs %0, esr_el1" : "=r"(esr));
    if ((esr >> 26) == 0x07) {
        rt_fp_trap_cur();              /* 站33:陷阱体抽出共用 */
    } else {
        /* 站21B:未知 EC 不再静默 wfi 停车(排障全靠猜)——ESR/ELR/
         * FAR 此刻架构有效,印完即终态停泊(他核照跑,宿主可处决)。 */
        rt_panic_fault("unhandled sync exception");
    }
}

/* 站20:fpdual A/B 同优先级(11/11)+两段精确分割(段间 rt_sleep_ticks(1)
 * 强制交替):持有权轮转确定性非零(fp-saves 有分子),值恒精确——
 * 分割点 700000,Σ(1..700000)+Σ(700001..1000000)=500000500000,
 * Σ(i*0.5) 同律分半;段和 <2^53 double 精确,fcvtzu 入 GPR 收口。
 * (旧 11/12 异级串行:A 先跑完退场清主 B 才起步,首陷阱恒见旧主
 * NULL,saves 恒 0,fpstat 判据失去分子,门禁实测;第一版分段改写
 * 的 asm 两雷——fadd 吃 FP 寄存器编译炸、fcvtzu "+r" 覆写非累加,
 * 且段界 off-by-one——本版全数修正。) */
static void fpdual_range_sum(u64 lo, u64 hi, u64 *acc)
{
    u64 tmp;

    __asm__ volatile(
        "fmov  d0, xzr\n\t"
        "mov   x9, %1\n\t"
        "1:\n\t"
        "scvtf d1, x9\n\t"
        "fadd  d0, d0, d1\n\t"
        "add   x9, x9, #1\n\t"
        "cmp   x9, %2\n\t"
        "b.le  1b\n\t"
        "fcvtzu %0, d0\n\t"
        : "=r"(tmp)
        : "r"(lo), "r"(hi)
        : "v0", "v1", "x9", "cc");
    *acc += tmp;
}

static void fpdual_range_half(u64 lo, u64 hi, u64 *acc)
{
    u64 tmp;

    __asm__ volatile(
        "fmov  d0, xzr\n\t"
        "fmov  d2, #0.5\n\t"
        "mov   x9, %1\n\t"
        "1:\n\t"
        "scvtf d1, x9\n\t"
        "fmul  d1, d1, d2\n\t"
        "fadd  d0, d0, d1\n\t"
        "add   x9, x9, #1\n\t"
        "cmp   x9, %2\n\t"
        "b.le  1b\n\t"
        "fcvtzu %0, d0\n\t"
        : "=r"(tmp)
        : "r"(lo), "r"(hi)
        : "v0", "v1", "v2", "x9", "cc");
    *acc += tmp;
}

/* 站二十:fpsmp 双核 FP 自证——A(核 0)整段 Σ1..1e6,B(核 1)
 * 半值段 Σ(i×0.5);分驻两核先后各起,per-CPU FP 帧(rt_fp_owner_arr)
 * 各自轮转,双精确 = 每核持有权机制独立无损。 */
void rt_fp_smp_a_task(void *arg)
{
    u64 got = 0;

    (void)arg;
    fpdual_range_sum(1ULL, 1000000ULL, &got);
    if (got == 500000500000ULL)
        rt_puts("fpsmp: cpu0 A OK: 500000500000\n");
    else
        rt_puts_u64("fpsmp: cpu0 A FAIL: ", got, "\n");
}

void rt_fp_smp_b_task(void *arg)
{
    u64 got = 0;

    (void)arg;
    fpdual_range_half(1ULL, 1000000ULL, &got);
    if (got == 250000250000ULL)
        rt_puts("fpsmp: cpu1 B OK: 250000250000\n");
    else
        rt_puts_u64("fpsmp: cpu1 B FAIL: ", got, "\n");
}

void rt_fp_dual_a_task(void *arg)
{
    u64 got = 0;

    (void)arg;
    fpdual_range_sum(1ULL, 700000ULL, &got);
    rt_sleep_ticks(1);               /* 段界让位:A/B 必交替 */
    fpdual_range_sum(700001ULL, 1000000ULL, &got);
    if (got == 500000500000ULL)
        rt_puts("fpdual: A OK: 500000500000\n");
    else
        rt_puts_u64("fpdual: A FAIL: ", got, "\n");
}

void rt_fp_dual_task(void *arg)
{
    u64 got = 0;

    (void)arg;
    fpdual_range_half(1ULL, 700000ULL, &got);
    rt_sleep_ticks(1);               /* 段界让位(同 A) */
    fpdual_range_half(700001ULL, 1000000ULL, &got);
    if (got == 250000250000ULL)
        rt_puts("fpdual: B OK: 250000250000\n");
    else
        rt_puts_u64("fpdual: B FAIL: ", got, "\n");
}


