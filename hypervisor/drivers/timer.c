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
    ticks++;
    timer_arm_next();
}

u64 timer_ticks(void)
{
    return ticks;
}
