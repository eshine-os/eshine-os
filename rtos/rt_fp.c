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

void rt_fp_dual_a_task(void *arg)
{
    (void)arg;
    if (fp_sum_check(1000000ULL, 500000500000ULL))
        rt_puts("fpdual: A OK: 500000500000\n");
    else
        rt_puts("fpdual: A FAIL: fp state corrupted\n");
}

void rt_fp_dual_task(void *arg)
{
    u64 got;

    (void)arg;
    __asm__ volatile(
        "fmov  d0, xzr\n\t"          /* acc = 0.0 */
        "fmov  d2, #0.5\n\t"
        "mov   x9, #1\n\t"
        "1:\n\t"
        "scvtf d1, x9\n\t"           /* d1 = (double)x9 */
        "fmul  d1, d1, d2\n\t"       /* d1 = x9 * 0.5 */
        "fadd  d0, d0, d1\n\t"
        "add   x9, x9, #1\n\t"
        "cmp   x9, %1\n\t"
        "b.lt  1b\n\t"
        "fcvtzu %0, d0\n\t"
        : "=r"(got)
        : "r"(1000001ULL)
        : "v0", "v1", "v2", "x9", "cc");
    __asm__ volatile("" : "+r"(got));   /* 活性屏:防比较被重排 */
    if (got == 250000250000ULL)
        rt_puts("fpdual: B OK: 250000250000\n");
    else
        rt_puts_u64("fpdual: B FAIL: ", got, "\n");
}
