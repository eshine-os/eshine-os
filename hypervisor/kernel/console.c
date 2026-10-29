/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "kernel/include/console.h"
#include "drivers/include/pl011.h"
#include "kernel/include/vcpu.h"

/* 站十八D:console 自旋锁(双 CPU 并发打印,判据行不许串字)。
 * 按 owner 递归:同 CPU 嵌套(guest_fault 整段持锁、内层 puts 复入)
 * 计数即可;跨 CPU 互斥用 ldaxr/stxr + WFE(临界区有界=行级)。
 * 持锁期间本 CPU 无嵌套打印的 IRQ 路径(异常进入自带 DAIF 屏),
 * 宿主线程 TCG 轮转下短暂自旋有界。 */
static volatile unsigned long con_lock;
static volatile long con_owner = -1;
static volatile unsigned long con_nest;

void console_lock(void)
{
    if (con_owner == (long)cpu_id()) {
        con_nest++;
        return;
    }
    for (;;) {
        unsigned long v;

        __asm__ volatile("ldaxr %0, [%1]" : "=r"(v) : "r"(&con_lock));
        if (!v) {
            unsigned long ok;

            __asm__ volatile("stxr %w0, %w2, [%1]"
                             : "=&r"(ok)
                             : "r"(&con_lock), "r"(1UL));
            if (!ok) {
                con_owner = (long)cpu_id();
                con_nest = 1;
                return;
            }
        }
        __asm__ volatile("wfe");
    }
}

void console_unlock(void)
{
    if (--con_nest == 0UL) {
        con_owner = -1;
        __asm__ volatile("stlr %0, [%1]" :: "r"(0UL), "r"(&con_lock));
        __asm__ volatile("sev");
    }
}

void console_init(void)
{
    pl011_init();
}

void console_putc(char c)
{
    /* 站22A([54] 三案定谳):原语级持锁。曾以为是"vuart fg 透传"
     * 一个裸写者,实测(probe-tear)宿主 shell 回显/vmrx dump 同类
     * ——跨核锁内 emit 拦不住锁外逐字写,两流互嵌撕行
     * ("spinni[vm2]…n g0.000000"字节级互撕实测定谳)。按 owner
     * 递归:puts/emit/vuart_tx_emit 复入只增嵌套,行级调用者零影响;
     * 单字符自身原子,行级原子仍由各调用方的锁窗负责。 */
    console_lock();
    if (c == '\n')
        pl011_putc('\r');              /* 终端换行惯例 */
    pl011_putc(c);
    console_unlock();
}

void console_puts(const char *s)
{
    console_lock();
    while (*s)
        console_putc(*s++);
    console_unlock();
}

void console_puthex(u64 v)
{
    static const char hex[] = "0123456789abcdef";
    char buf[17];
    int i;

    buf[16] = '\0';
    for (i = 15; i >= 0; i--) {
        buf[i] = hex[v & 0xf];
        v >>= 4;
    }
    console_puts(buf);                 /* 复用 puts 的行级锁 */
}
