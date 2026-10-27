/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "drivers/include/timer.h"

static u64 ticks;
static u64 freq;

static void timer_arm_next(void)
{
    u64 now;

    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(now));
    __asm__ volatile("msr cnthp_cval_el2, %0" :: "r"(now + freq));
    __asm__ volatile("msr cnthp_ctl_el2, %0" :: "r"(1UL));   /* enable, 未掩 */
}

void timer_init(void)
{
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    ticks = 0;
    timer_arm_next();
}

void timer_handler(void)
{
    u64 m;

    /* 站19D:三核各起一拍,共享计数只由 CPU0 记账(3 拍/秒的漂移
     * 不许进宿主 uptime);次级的 26 = 纯兜底面(sweep 驱动+泵醒)。 */
    __asm__ volatile("mrs %0, mpidr_el1" : "=r"(m));
    if ((m & 0xffUL) == 0UL)
        ticks++;
    timer_arm_next();
}

u64 timer_ticks(void)
{
    return ticks;
}
