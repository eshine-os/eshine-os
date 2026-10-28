/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "rtos.h"
#include "miniuart.h"

/* RTOS 自有 RX 环:minuart.h 的 static 环 per-TU,多 TU 各持一份会
 * 串不成一环——本环是唯一真环,ISR 与壳都走这里。 */

#define RRX_SIZE 64u
static char rrx[RRX_SIZE];
static volatile unsigned int rrx_h, rrx_t;

void rt_uart_drain_isr(void)
{
    while (!(uart_read(UART_FR) & FR_RXFE)) {
        unsigned int n = (rrx_h + 1) % RRX_SIZE;
        char c = (char)uart_read(UART_DR);

        if (n != rrx_t) {
            rrx[rrx_h] = c;
            rrx_h = n;
        }                             /* 环满丢新(读 DR 已清挂起) */
    }
}

int rt_getc_nb(void)
{
    unsigned int t = rrx_t;

    if (t == rrx_h)
        return -1;
    rrx_t = (t + 1) % RRX_SIZE;
    return (unsigned char)rrx[t];
}

int rt_rx_pending(void)
{
    return rrx_h != rrx_t;
}

/* 行级打印临界区:多任务并发打印以行原子(心跳/自证/壳不打架),
 * 行间交错正是并发判据。窗口 = 一行几十次 MMIO 陷阱,100Hz demo
 * 尺度无延迟之虞。 */
void rt_puts(const char *s)
{
    __asm__ volatile("msr daifset, #2");
    puts_uart(s);
    __asm__ volatile("msr daifclr, #2");
}

void rt_puts_u64(const char *pfx, u64 v, const char *sfx)
{
    char digits[24];
    unsigned int i = sizeof(digits) - 1;

    digits[i] = '\0';
    do {
        digits[--i] = (char)('0' + (v % 10));
        v /= 10;
    } while (v);
    rt_puts(pfx);
    rt_puts(&digits[i]);
    rt_puts(sfx);
}
