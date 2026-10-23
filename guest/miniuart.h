/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#ifndef GUEST_MINIUART_H
#define GUEST_MINIUART_H

/* QEMU virt PL011。guest 走纯 stage-2(SCTLR_EL1.M=0),VA 即 IPA:
 * 0x09000000 由 hypervisor 的 stage-2 设备页(nGnRE + XN)放行。
 * TX 直访真硬件;RX 走 HVC #3 代理(输入所有权,见 uart_getc)。 */
#define UART0_BASE 0x09000000UL

#define UART_DR   0x00u
#define UART_FR   0x18u                 /* PL011 FR=0x18;0x14 未实现,读恒 0
                                          * → 曾致 RX 无门控热自旋,与 QEMU
                                          *   chardev 推流赛跑,偶发字节重复/丢失 */
#define FR_RXFE   (1u << 4)
#define FR_TXFF   (1u << 5)

static volatile unsigned int *uart_reg(unsigned long off)
{
    return (volatile unsigned int *)(UART0_BASE + off);
}

static inline void uart_putc(char c)
{
    while (*uart_reg(UART_FR) & FR_TXFF)
        ;
    *uart_reg(UART_DR) = (unsigned char)c;
}

#include "hvc.h"

/* RX 经 HVC #3 代理(宿主按 console owner 路由):双 guest 直读 DR 会
 * 互抢输入,后台模式还会吃掉宿主命令。TX(putc)保持直访。
 * 三态:-2=无输入权(后台)——WFI 让位,TWI 陷阱化下宿主泵去跑别家,
 * 免得空转 HVC 风暴;-1=有输入权暂无字符(前台轮询);>=0=字符。 */
static inline char uart_getc(void)
{
    long c;

    do {
        c = hvc_getc();
        if (c == -2)
            __asm__ volatile("wfi");
    } while (c < 0);
    return (char)c;
}

#endif /* GUEST_MINIUART_H */
