/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#ifndef GUEST_MINIUART_H
#define GUEST_MINIUART_H

/* QEMU virt PL011。guest 走纯 stage-2(SCTLR_EL1.M=0),VA 即 IPA。
 * 第六站:UART IPA 已撤出 stage-2 映射(仿真窗口)——DR/FR 访问全部
 * data abort 陷阱化,宿主 per-VM vuart 按寄存器语义仿真。驱动不改
 * 语义:TX 轮询代码零改动;RX 从 HVC 三态换回 PL011 原生 FR 轮询。 */
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

/* RX:FR.RXFE 轮询,空则 WFI 让位(TWI 陷阱化 -> 宿主 WAIT;输入
 * 入环即直通唤醒)。同一仿真窗口承担 TX/RX,HVC #0/#3 已退役
 * (第六站:超调用只剩控制类)。 */
static inline char uart_getc(void)
{
    while (*uart_reg(UART_FR) & FR_RXFE)
        __asm__ volatile("wfi");
    return (char)*uart_reg(UART_DR);
}

#endif /* GUEST_MINIUART_H */
