/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "kernel/include/console.h"
#include "kernel/include/panic.h"

void panic(const char *msg)
{
    console_puts("\n[panic] ");
    console_puts(msg);
    console_puts("\n");
    for (;;)
        __asm__ volatile("wfi");
}
