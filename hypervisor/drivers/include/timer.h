/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#ifndef ESHINE_TIMER_H
#define ESHINE_TIMER_H

#include "kernel/include/types.h"

void timer_init(void);          /* CNTHP 武装,1s 周期 */
void timer_handler(void);       /* el2_irq 分发入口:tick++ + 重装 */
u64  timer_ticks(void);         /* tick 读数(64 位对齐访问,原子) */

#endif /* ESHINE_TIMER_H */
