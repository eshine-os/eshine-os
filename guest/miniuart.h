/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#ifndef GUEST_MINIUART_H
#define GUEST_MINIUART_H

/* QEMU virt PL011。guest 走纯 stage-2(SCTLR_EL1.M=0),VA 即 IPA:
 * 0x09000000 由 hypervisor 的 stage-2 设备页(nGnRE + XN)放行,
 * 不经 HVC、不经 EL1 stage-1,直接命中真硬件。 */
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

static inline char uart_getc(void)
{
    while (*uart_reg(UART_FR) & FR_RXFE)
        ;
    return (char)*uart_reg(UART_DR);
}

#endif /* GUEST_MINIUART_H */
