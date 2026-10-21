/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#ifndef ESHINE_MM_H
#define ESHINE_MM_H

#include "kernel/include/types.h"

/* VTCR_EL2: TG0=4KB(0b00) | SH0=IS(0b11) | ORGN0=WB | IRGN0=WB | SL0=L1(0b01)
 *           | PS=40bit(0b010) | T0SZ=32 */
#define VTCR_VALUE  (0x2UL<<16 | 0x1UL<<6 | 0x3UL<<12 | 0x3UL<<10 | 0x3UL<<8 | 32UL)

/* stage-2 描述符 MemAttr 编码(ARM DDI 0487) */
#define S2_ATTR_NORMAL   0xFUL   /* S1 关时按 Normal WB 处理 */
#define S2_ATTR_DEVICE   0x1UL   /* nGnRE */

void stage2_init(void);
int  stage2_walk(u64 ipa, u64 *pa);
void stage2_dump(void);
u64 *stage2_pgd(void);

#endif /* ESHINE_MM_H */
