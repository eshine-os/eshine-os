/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#ifndef ESHINE_CONSOLE_H
#define ESHINE_CONSOLE_H

#include "kernel/include/types.h"

void console_init(void);
void console_putc(char c);
void console_puts(const char *s);
void console_puthex(u64 v);

/* 站十八D:双核并发打印的行级原子锁(按 owner 可递归) */
void console_lock(void);
void console_unlock(void);

#endif /* ESHINE_CONSOLE_H */
