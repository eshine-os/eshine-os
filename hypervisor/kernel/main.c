/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "kernel/include/console.h"
#include "kernel/include/shell.h"
#include "kernel/include/version.h"

void main(void)
{
    console_init();
    console_puts("\n" ESHINE_BANNER "\n");
    shell_run();
}
