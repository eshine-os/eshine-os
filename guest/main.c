/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "miniuart.h"
#include "virtio.h"
#include "gic.h"
#include "hvc.h"
#include "timer.h"
#include "fdt.h"

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
/* virtio 完成计数在 virtio.h(virtio_done) */

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
    if (iar == GUEST_INTID_UART) {
        uart_rx_isr();               /* 读 DR 入软环(虚 EOI 即撤,无物理线) */
    }
    if (iar == GUEST_INTID_VIRTIO) {
        virtio_isr();                /* 读 InterruptStatus/ACK 清挂起(第十站) */
    }
    if (iar != GUEST_INTID_SPURIOUS)
        gic_eoir(iar);   /* 虚 EOI:vtimer 经 HW-LR 联动物理线,uart 纯虚拟直撤 */
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

    gic_dist_init();                 /* 第十二站:vGIC 真启动式 bring-up(门禁 [12-13]) */
    gic_guest_enable();              /* ICV 面就绪并解 IRQ 后才可碰定时器 */
    uart_irq_enable();              /* 第八站:RXIM 使能,宿主入环即虚注 33 */
    virtio_blk_init();               /* 第十站:probe+Status 步进+队列+DRIVER_OK */
    puts_uart(BANNER);              /* 第六站:全输出面经 vuart 陷阱仿真 */
    fdt_check();                    /* 第十一站:硬件自描述自查(门禁 [9-11]) */
    puts_uart("uart irq on\n");     /* 中断驱动模式的显式判据(门禁 needle) */
    puts_uart("virtio blk on\n");   /* virtio 就绪判据(第十站 needle,probe 三验+DRIVER_OK) */
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
            } else if (starts(linebuf, "sleep")) {
                unsigned int secs = 2;

                if (linebuf[5] == ' ' && linebuf[6] >= '1' && linebuf[6] <= '9'
                    && linebuf[7] == '\0')
                    secs = (unsigned int)(linebuf[6] - '0');
                else if (linebuf[5] != '\0')
                    secs = 0;                     /* 参数不合法:走 usage */
                if (secs == 0) {
                    puts_uart("usage: sleep [1-9]\n");
                } else {
                    puts_uart("sleeping...\n");
                    vtimer_arm_seconds(secs);
                    while (!wake_count)   /* WFI 陷阱回来重查:WOKE 注入才到 */
                        __asm__ volatile("wfi" ::: "memory");
                    wake_count = 0;
                    puts_uart("slept (vtimer irq)\n");
                }
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
            } else if (streq(linebuf, "dwrite")) {
                /* virtio-blk 写全链:三描述符链(header/data/status)
                 * -> avail 入队 -> 门铃 -> WFI -> 完成中断(35) ->
                 * ISR 清挂起 -> 醒来回报 */
                char *p = g_data;
                const char *msg = "ESHINE-BLK-persist";

                while (*msg)
                    *p++ = *msg++;
                virtio_blk_do(1);
                puts_uart("blk wrote\n");
            } else if (streq(linebuf, "dread")) {
                g_data[0] = 0;
                virtio_blk_do(0);
                puts_uart("blk read: ");
                puts_uart(g_data);
                puts_uart("\n");
            } else if (streq(linebuf, "fault")) {
                puts_uart("triggering unmapped read...\n");
                (void)*(volatile u64 *)0x30000000UL;   /* 永不返回:VM 隔离死 */
                puts_uart("unreachable\n");
            } else if (starts(linebuf, "echo ")) {
                puts_uart(linebuf + 5);
                puts_uart("\n");
            } else if (linebuf[0]) {
                puts_uart("guest: try echo <text> | sleep | spin | dwrite | dread | host | exit\n");
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
