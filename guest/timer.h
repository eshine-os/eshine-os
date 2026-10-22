/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#ifndef GUEST_TIMER_H
#define GUEST_TIMER_H

typedef unsigned long long u64;   /* 与 hvc.h 同型,C11 允许重复定义 */

/* guest 虚拟定时器(CNTV)直访:计数器经 stage-2 物理直通,无陷阱无偏移。
 * 到期信号走 GIC INTID 27:宿主 HW-LR 注入,guest 在自己的向量里认领。 */

static inline u64 vtimer_freq(void)
{
    u64 v;

    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(v));
    return v;
}

static inline u64 vtimer_now(void)
{
    u64 v;

    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(v));
    return v;
}

static inline void vtimer_arm_seconds(u64 seconds)
{
    __asm__ volatile("msr cntv_cval_el0, %0"
                     :: "r"(vtimer_now() + seconds * vtimer_freq()));
    __asm__ volatile("msr cntv_ctl_el0, %0" :: "r"(1UL));
}

static inline void vtimer_disable(void)
{
    __asm__ volatile("msr cntv_ctl_el0, %0" :: "r"(0UL));
}

#endif /* GUEST_TIMER_H */
