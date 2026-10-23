/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
/* QEMU virt UART0, 轮询模型(基准对齐: 与参考实现同款外设选择) */
#include "drivers/include/pl011.h"

#define UART0_BASE      0x09000000UL
#define UART_DR         0x00
#define UART_FR         0x18
#define UART_IBRD       0x24
#define UART_FBRD       0x28
#define UART_LCRH       0x2c
#define UART_CR         0x30
#define UART_IMSC       0x38
#define UART_ICR        0x44

#define FR_TXFF         (1u << 5)
#define FR_RXFE         (1u << 4)

#define LCRH_FEN        (1u << 6)
#define LCRH_WLEN_8N1   (3u << 5)

#define CR_UARTEN       (1u << 0)
#define CR_TXE          (1u << 8)
#define CR_RXE          (1u << 9)

static inline void wr(u32 off, u32 v)
{
    *(volatile u32 *)(UART0_BASE + off) = v;
}

static inline u32 rd(u32 off)
{
    return *(volatile u32 *)(UART0_BASE + off);
}

void pl011_init(void)
{
    wr(UART_CR, 0);                        /* 先关再配 */
    wr(UART_ICR, 0x7ff);                   /* 清全部中断 */
    wr(UART_IBRD, 13);                     /* 24 MHz / 16 / 115200 ≈ 13.02 */
    wr(UART_FBRD, 1);
    wr(UART_LCRH, LCRH_FEN | LCRH_WLEN_8N1);
    wr(UART_IMSC, 0);                      /* 轮询,不使能中断 */
    wr(UART_CR, CR_UARTEN | CR_TXE | CR_RXE);
}

void pl011_putc(char c)
{
    while (rd(UART_FR) & FR_TXFF)
        ;
    wr(UART_DR, (u8)c);
}

char pl011_getc(void)
{
    while (rd(UART_FR) & FR_RXFE)
        ;
    return (char)(rd(UART_DR) & 0xff);
}

int pl011_getc_poll(void)
{
    if (rd(UART_FR) & FR_RXFE)
        return -1;                      /* 无输入:泵跑 guest(shell 权威) */
    return (int)(rd(UART_DR) & 0xff);
}
