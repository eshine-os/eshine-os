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
 * 尺度无延迟之虞。
 * 站二十:DAIF 屏只防本核抢切,双核下心跳行会互串(站20 [53] 的
 * 教训反用)——控制台自旋锁补跨核互斥;本核无争用时锁即屏等价。 */
static unsigned long puts_lk;

void rt_puts(const char *s)
{
    u64 d = rt_spin_lock(&puts_lk);

    puts_uart(s);
    rt_spin_unlock(&puts_lk, d);
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
    /* 站二十:整行组串后一次 rt_puts——旧版三段各自加锁,双核下
     * 段间互串("cpu1: n=cpu0: n=…" smpdemo 实测定谳):rt_puts 只
     * 保证单次调用的行原子,多段拼行必须先组好。 */
    {
        char line[96];
        unsigned int n = 0, j;

        for (j = 0; pfx[j] && n < sizeof(line) - 1; j++)
            line[n++] = pfx[j];
        for (j = i; digits[j] && n < sizeof(line) - 1; j++)
            line[n++] = digits[j];
        for (j = 0; sfx[j] && n < sizeof(line) - 1; j++)
            line[n++] = sfx[j];
        line[n] = '\0';
        rt_puts(line);
    }
}

/* 站21B:十六进制诊断行——裸打(puts_uart 直连,不取 puts_lk):
 * panic/未知异常语境可能正打断一个持锁打印,take 锁=同核自死锁
 * (自旋锁契约"同核递归=死锁");判据窗无并发打印者,零锁代价换
 * panic 可达。行组串同 rt_puts_u64。 */
void rt_puts_hex(const char *pfx, u64 v, const char *sfx)
{
    char line[64];
    unsigned int n = 0, j;
    static const char hex[] = "0123456789abcdef";
    int k;

    for (j = 0; pfx[j] && n < sizeof(line) - 1; j++)
        line[n++] = pfx[j];
    for (k = 60; k > 0; k -= 4)
        if ((v >> k) & 0xFu)
            break;                      /* 去前导零(判据 needle 取
                                          * "esr=0x" 前缀即稳) */
    for (; k >= 0 && n < sizeof(line) - 1; k -= 4)
        line[n++] = hex[(v >> k) & 0xFu];
    for (j = 0; sfx[j] && n < sizeof(line) - 1; j++)
        line[n++] = sfx[j];
    line[n] = '\0';
    puts_uart(line);
}

/* 站28:长度式行级打印——sys_write 面的内核落点(buf 非 NUL 终止);
 * 同 puts_lk 串行化(与 rt_puts/心跳行互不打架,行级原子契约同律)。 */
void rt_puts_n(const char *s, u32 len)
{
    u64 d = rt_spin_lock(&puts_lk);

    for (u32 i = 0; i < len; i++)
        uart_putc(s[i]);
    rt_spin_unlock(&puts_lk, d);
}
