/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#ifndef ESHINE_CONSOLE_H
#define ESHINE_CONSOLE_H

#include "kernel/include/types.h"

void console_init(void);
void console_putc(char c);
void console_puts(const char *s);
void console_puthex(u64 v);

#endif /* ESHINE_CONSOLE_H */
