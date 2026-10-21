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

/* ---- 27.04 第二站:per-VM stage-2(类型在 mm.h,避免 vm.h↔mm.h 循环) ---- */

/* VM 内存区:2MB block 粒度,RAM 段(IPA 1-2GB)内 */
struct mem_region { u64 ipa, pa, size, attr; };
/* VM 设备页:4KB 粒度,设备段(IPA 0-1GB)内,构建时强制 XN */
struct vm_device  { u64 ipa, pa, size, attr; };

/* per-VM stage-2 全套表;topology 与第一站一致:
 *   pgd[0] -> l2_dev(设备段 L2)   pgd[1] -> l2_ram(RAM 段 L2)
 *   l3_dev 由首个设备页的 l2 表项指向(共享)
 * 每张表独立 4KB 对齐页 — DSC_TABLE 掩码砍低 12 位,任何表
 * 不落 4KB 边界都会被指错(gdb 实测:未对齐时 l2_dev 指针折回 pgd 页首)。 */
struct vm_space {
    u64 pgd[512]    __attribute__((aligned(4096)));  /* T0SZ=32 → 仅前 4 项有效 */
    u64 l2_dev[512] __attribute__((aligned(4096)));
    u64 l2_ram[512] __attribute__((aligned(4096)));
    u64 l3_dev[512] __attribute__((aligned(4096)));
    u64 vttbr;                                      /* pgd | vmid<<48 */
};

/* 从配置构建 per-VM 表;配置越界/不对齐返回 -1(调用方礼貌拒绝) */
int stage2_build(struct vm_space *sp, u64 vmid,
                 const struct mem_region *mem, u32 n_mem,
                 const struct vm_device *dev, u32 n_dev);

#endif /* ESHINE_MM_H */
