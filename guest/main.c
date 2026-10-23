/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "gic.h"
#include "hvc.h"
#include "miniuart.h"
#include "timer.h"

/* 同源双镜像:-DGUEST_ID=0/1,身份仅差 banner 与提示符(spec §5) */
#ifndef GUEST_ID
#define GUEST_ID 0
#endif

#if GUEST_ID == 0
#define BANNER "eshine-guest VM0 @EL1 via stage-2 (vuart @trap)\n"
#define PROMPT "vm0> "
#elif GUEST_ID == 1
#define BANNER "eshine-guest VM1 @EL1 via stage-2 (vuart @trap)\n"
#define PROMPT "vm1> "
#else
#error "GUEST_ID must be 0 or 1"
#endif

#define GBUF_MAX 128

static char linebuf[GBUF_MAX];
static volatile unsigned int wake_count;   /* vtimer IRQ 计数(sleep 唤醒判据) */

/* guest 自己的 IRQ 处理:认领走 ICV 视图(HCR.IMO 重定向,EL2 的
 * vgic_inject_vtimer 已把 vINTID 27 挂上)。关定时器在 EOI 前:
 * 电平线先降,EOIR(降+撤)再联动撤销物理线,不重挂起。 */
void guest_irq_c(void)
{
    u64 iar = gic_claim();

    if (iar == GUEST_INTID_VTIMER) {
        vtimer_disable();
        wake_count++;
    }
    if (iar != GUEST_INTID_SPURIOUS)
        gic_eoir(iar);   /* 虚 EOI:硬件联动撤销宿主物理线(HW-LR) */
}

static void puts_uart(const char *s)
{
    while (*s)
        uart_putc(*s++);
}

static int streq(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static int starts(const char *s, const char *pfx)
{
    while (*pfx) {
        if (*s++ != *pfx++)
            return 0;
    }
    return 1;
}

/* guest 侧三件套:HVC banner(超调用)、直访 UART banner(stage-2 设备页)、
 * 行编辑交互(echo/exit)。exit 经 HVC #1 退回 EL2。 */
void guest_main(void)
{
    unsigned int idx = 0;

    gic_guest_enable();              /* ICV 面就绪并解 IRQ 后才可碰定时器 */
    puts_uart(BANNER);              /* 第六站:全输出面经 vuart 陷阱仿真 */
    puts_uart(PROMPT);

    for (;;) {
        char c = uart_getc();

        if (c == '\r' || c == '\n') {
            uart_putc('\n');
            linebuf[idx] = '\0';
            if (streq(linebuf, "exit")) {
                hvc_exit();                 /* noreturn,宿主接手打印 VM_DEAD */
            } else if (streq(linebuf, "yield")) {
                puts_uart("yielding...\n");
                hvc_yield();
                puts_uart("resumed\n");     /* 续跑点:从此处继续 */
            } else if (streq(linebuf, "selftest")) {
                u64 sum = 0;
                unsigned int i;

                for (i = 0; i < 100; i++) {
                    sum += i;
                    __asm__ volatile("" : "+r"(sum));   /* 挡常量折叠:sum 必须真活在寄存器 */
                    uart_putc('.');   /* 100 次 MMIO 陷阱:寄存器存活性判据比 HVC 更硬 */
                }
                if (sum == 4950)
                    puts_uart("\nselftest OK\n");
                else
                    puts_uart("\nselftest FAIL\n");
            } else if (streq(linebuf, "sleep")) {
                puts_uart("sleeping...\n");
                vtimer_arm_seconds(2);
                while (!wake_count)   /* WFI 陷阱回来重查:宿主陪睡后注入才到 */
                    __asm__ volatile("wfi" ::: "memory");
                wake_count = 0;
                puts_uart("slept (vtimer irq)\n");
            } else if (streq(linebuf, "host")) {
                puts_uart("releasing console...\n");
                hvc_host();                  /* 交还 RX;此后 -2 -> WFI 让位 */
            } else if (streq(linebuf, "spin")) {
                /* 迭代忙循环:不可让出负载。宿主 tick 在任意指令边界把本
                 * vCPU 抢占切走再无损续跑——计数器的存活性与最终输出即
                 * 转写正确性判据。3G 迭代(TCG 实测 ~0.4G 迭代/s,
                 * 约 8s)跨约 8 个量子,preempts 前后差为门禁判据。
                 * (时基版曾在 PREEMPT 边界下观察到 mrs 计数循环不退,
                 * QEMU helper 交互疑点,已改纯迭代,见 devlog 05。) */
                u64 cnt = 3000000000ULL;

                puts_uart("spinning...\n");
                while (cnt > 0) {
                    cnt--;
                    __asm__ volatile("" ::: "memory");
                }
                puts_uart("spun 3G iters (preempted ok)\n");
            } else if (streq(linebuf, "fault")) {
                puts_uart("triggering unmapped read...\n");
                (void)*(volatile u64 *)0x30000000UL;   /* 永不返回:VM 隔离死 */
                puts_uart("unreachable\n");
            } else if (starts(linebuf, "echo ")) {
                puts_uart(linebuf + 5);
                puts_uart("\n");
            } else if (linebuf[0]) {
                puts_uart("guest: try echo <text> | sleep | spin | host | exit\n");
            }
            idx = 0;
            puts_uart(PROMPT);
        } else if ((c == 0x7f || c == '\b') && idx > 0) {
            idx--;
            puts_uart("\b \b");
        } else if (idx < GBUF_MAX - 1 && c >= 0x20 && c < 0x7f) {
            linebuf[idx++] = c;
            uart_putc(c);
        }
    }
}
