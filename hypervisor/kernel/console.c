/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "kernel/include/console.h"
#include "drivers/include/pl011.h"

void console_init(void)
{
    pl011_init();
}

void console_putc(char c)
{
    if (c == '\n')
        pl011_putc('\r');              /* 终端换行惯例 */
    pl011_putc(c);
}

void console_puts(const char *s)
{
    while (*s)
        console_putc(*s++);
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
    console_puts(buf);
}
