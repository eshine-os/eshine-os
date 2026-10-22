/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "drivers/include/gicv3.h"
#include "drivers/include/timer.h"
#include "kernel/include/console.h"
#include "kernel/include/shell.h"
#include "kernel/include/vcpu.h"
#include "kernel/include/version.h"

void main(void)
{
    console_init();
    gicv3_init();                     /* GIC 先于解掩(顺序即安全边界) */
    timer_init();
    vcpu_init();                       /* SP_EL2 异常栈,越早越稳 */
    console_puts("\n" ESHINE_BANNER "\n");
    __asm__ volatile("msr daifclr, #2");   /* 解 IRQ:中断面就绪后才可 */
    shell_run();
}
