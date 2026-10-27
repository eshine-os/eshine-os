/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "drivers/include/gicv3.h"
#include "drivers/include/timer.h"
#include "kernel/include/console.h"
#include "kernel/include/sched.h"
#include "kernel/include/shell.h"
#include "kernel/include/vcpu.h"
#include "kernel/include/version.h"

/* 站十八D:PSCI CPU_ON 唤起次核(QEMU virt 上电即 PSCI off,探针
 * 实证;conduit=smc——DTB psci 节点 method 字段为准,el2_sync 现形
 * 即改 hvc 一步定谳)。entry 必须物理地址 = EL2 恒等链接地址。 */
static void smp_boot_secondary(void)
{
    u64 ret = 0;
    u64 entry, ctx = 0x1234;

    __asm__ volatile("ldr %0, =secondary_entry" : "=r"(entry));
    __asm__ volatile(
        "mov x0, %1\n\t"
        "mov x1, %2\n\t"
        "mov x2, %3\n\t"
        "mov x3, %4\n\t"
        "smc #0\n\t"
        "mov %0, x0"
        : "=r"(ret)
        : "r"(0xc4000003UL), "r"(1UL), "r"(entry), "r"(ctx)
        : "x0", "x1", "x2", "x3");
    console_puts("psci cpu_on -> ");
    console_puthex(ret);               /* 0 = PSCI_SUCCESS */
    console_puts("\n");
}

void main(void)
{
    console_init();
    gicv3_init();                     /* GIC 先于解掩(顺序即安全边界) */
    timer_init();
    vcpu_init();                       /* SP_EL2 异常栈,越早越稳 */
    console_puts("\n" ESHINE_BANNER "\n");
    __asm__ volatile("msr daifclr, #2");   /* 解 IRQ:中断面就绪后才可 */
    smp_boot_secondary();               /* 站十八D:次核起立(探针定谳) */
    {
        /* 站十八D:有界等 CPU1 上线(握手先行)——判据 token 的逐字符
         * 回显不许被上线打印劈断(实测 d3 败因);~500ms 上限,
         * PSCI 被拒(-smp 1)时从容放弃。 */
        u64 t0;

        __asm__ volatile("mrs %0, cntvct_el0" : "=r"(t0));
        while (!sched_cpu1_up) {
            u64 now;

            __asm__ volatile("mrs %0, cntvct_el0" : "=r"(now));
            if (now - t0 > 31000000ULL)   /* ~500ms @ 62.5MHz */
                break;
        }
    }
    shell_run();
}
