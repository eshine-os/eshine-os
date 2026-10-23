/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
/* per-VM stage-2 构建器。4KB 粒度, T0SZ=32, SL0=1(L1 起), PS=40。
 * EL2 MMU 未开 ⇒ 所有地址即物理地址,表地址可直接取指针值。 */
#include "arch/arm64/include/mm.h"

/* 表描述符: valid|table + next table addr[51:12] */
#define DSC_TABLE(a)      (((u64)(a) & ~0xFFFULL) | 0x3UL)
/* 页/块描述符(DDI 0487 stage-2): addr | MemAttr[5:2] | S2AP[7:6]=RW
 *   | AF(bit10)=1 — AF 必置位,否则首访即 access-flag fault(门禁实测)
 *   | SH[9:8]=0 */
#define DSC_MAP(a, attr)  (((u64)(a) & ~0xFFFULL) | ((attr) << 2) | (0x3UL << 6) | (0x1UL << 10) | 0x1UL)
/* L3 页描述符 bit1=1 */
#define DSC_PAGE(a, attr) (DSC_MAP((a), (attr)) | 0x2UL)
/* XN[54:53]: EL0/EL1 均不可执行(设备页强制) */
#define DSC_XN            (0x3UL << 53)

#define BLK_2M  0x200000UL
#define PG_4K   0x1000UL

/* RAM 走 l2_ram 2MB block,设备页走 l2_dev→l3_dev 4KB page(强制 XN)。
 * 配置违规(越界/不对齐/表越界)返回 -1,不 panic —— 调用方可礼貌拒绝。 */
int stage2_build(struct vm_space *sp, u64 vmid,
                 const struct mem_region *mem, u32 n_mem,
                 const struct vm_device *dev, u32 n_dev)
{
    u32 i, b;

    for (i = 0; i < 4; i++)
        sp->pgd[i] = 0;
    for (i = 0; i < 512; i++) {
        sp->l2_dev[i] = 0;
        sp->l2_ram[i] = 0;
        sp->l3_dev[i] = 0;
    }
    sp->pgd[0] = DSC_TABLE(sp->l2_dev);
    sp->pgd[1] = DSC_TABLE(sp->l2_ram);

    for (i = 0; i < n_mem; i++) {
        const struct mem_region *r = &mem[i];
        u64 base = (r->ipa - 0x40000000UL) >> 21;

        if (r->size == 0 || (r->size & (BLK_2M - 1)) ||
            (r->ipa & (BLK_2M - 1)) || (r->pa & (BLK_2M - 1)))
            return -1;
        if (r->ipa < 0x40000000UL || r->ipa + r->size > 0x80000000UL)
            return -1;                       /* RAM 段必须在 IPA 1-2GB 内 */
        if (base + r->size / BLK_2M > 512)
            return -1;
        for (b = 0; b < r->size / BLK_2M; b++)
            sp->l2_ram[base + b] = DSC_MAP(r->pa + (u64)b * BLK_2M, r->attr);
    }

    for (i = 0; i < n_dev; i++) {
        const struct vm_device *d = &dev[i];
        u64 l2i = d->ipa >> 21;

        if (d->size != PG_4K || (d->ipa & (PG_4K - 1)))
            return -1;
        if (d->ipa >= 0x40000000UL)
            return -1;                       /* 设备段必须在 IPA 0-1GB 内 */
        if (d->attr == S2_ATTR_EMULATED)
            continue;   /* 仿真设备:不建映射,访问即 data abort 陷阱(第六站) */
        if (d->pa & (PG_4K - 1))
            return -1;
        sp->l2_dev[l2i] = DSC_TABLE(sp->l3_dev);
        sp->l3_dev[(d->ipa >> 12) & 0x1FF] = DSC_PAGE(d->pa, d->attr) | DSC_XN;
    }

    sp->vttbr = (u64)sp->pgd | ((vmid & 0xFFUL) << 48);
    return 0;
}
