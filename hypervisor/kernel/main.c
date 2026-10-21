/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "kernel/include/types.h"

void main(void)
{
    for (;;)
        __asm__ volatile("wfi");
}
