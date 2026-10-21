/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#ifndef ESHINE_PL011_H
#define ESHINE_PL011_H

#include "kernel/include/types.h"

void pl011_init(void);
void pl011_putc(char c);
char pl011_getc(void);

#endif /* ESHINE_PL011_H */
