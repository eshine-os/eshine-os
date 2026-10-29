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
static void psci_cpu_on(u64 mpidr, u64 ctx, u64 *ret)
{
    u64 entry;

    __asm__ volatile("ldr %0, =secondary_entry" : "=r"(entry));
    __asm__ volatile(
        "mov x0, %1\n\t"
        "mov x1, %2\n\t"
        "mov x2, %3\n\t"
        "mov x3, %4\n\t"
        "smc #0\n\t"
        "mov %0, x0"
        : "=r"(*ret)
        : "r"(0xc4000003UL), "r"(mpidr), "r"(entry), "r"(ctx)
        : "x0", "x1", "x2", "x3");
}

static void smp_boot_secondary(u64 mpidr, u64 ctx)
{
    u64 ret = 0xdeadbeef;

    psci_cpu_on(mpidr, ctx, &ret);
    {   /* 站20:三核并发下整行原子(同 smp.c 自报,分段印会互串) */
        char rep[48];
        unsigned int r = 0, i;
        static const char head[] = "psci cpu_on (";
        static const char mid[] = ") -> ";
        const char hx[] = "0123456789abcdef";
        u64 v = ret;

        for (i = 0; i < sizeof(head) - 1; i++)
            rep[r++] = head[i];
        if (mpidr <= 9u) {
            rep[r++] = (char)('0' + (u32)mpidr);
        } else {
            rep[r++] = '1';
            rep[r++] = (char)('0' + ((u32)mpidr - 10u));
        }
        for (i = 0; i < sizeof(mid) - 1; i++)
            rep[r++] = mid[i];
        for (i = 0; i < 16; i++)
            rep[r++] = hx[(v >> (60 - 4 * i)) & 0xFu];
        rep[r++] = '\n';
        rep[r] = '\0';
        console_puts(rep);
    }
}

void main(void)
{
    console_init();
    gicv3_init();                     /* GIC 先于解掩(顺序即安全边界) */
    timer_init();
    vcpu_init();                       /* SP_EL2 异常栈,越早越稳 */
    console_puts("\n" ESHINE_BANNER "\n");
    __asm__ volatile("msr daifclr, #2");   /* 解 IRQ:中断面就绪后才可 */
    smp_boot_secondary(1, 1);           /* 站十八D:CPU1(站19D ctx=核号) */
    smp_boot_secondary(2, 2);           /* 站十九D:CPU2(vm2.vcpu1 专泵) */
    smp_boot_secondary(3, 3);           /* 站二十:CPU3(vm0.vcpu1 专泵) */
    {
        /* 站十八D:有界等次核上线(握手先行)——判据 token 的逐字符
         * 回显不许被上线打印劈断(实测 d3 败因);~500ms 上限,
         * PSCI 被拒(-smp 1)时从容放弃。站19D:等 1 与 2;站20:加等 3。 */
        u64 t0;

        __asm__ volatile("mrs %0, cntvct_el0" : "=r"(t0));
        while (!sched_cpu_up[1] || !sched_cpu_up[2] || !sched_cpu_up[3]) {
            u64 now;

            __asm__ volatile("mrs %0, cntvct_el0" : "=r"(now));
            if (now - t0 > 31000000ULL)   /* ~500ms @ 62.5MHz */
                break;
        }
    }
    shell_run();
}
