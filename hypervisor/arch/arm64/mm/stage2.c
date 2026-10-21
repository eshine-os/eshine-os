/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
/* stage-2 translation tables. 4KB granule, T0SZ=32, SL0=1(L1 起), PS=40.
 * 固定映射(spec §2): IPA 0x40000000(2MB block)→PA 0x50000000 非恒等;
 *                     IPA 0x09000000(4KB page)→PA 0x09000000 设备恒等。
 * EL2 MMU 未开 ⇒ 所有地址即物理地址,表地址可直接取指针值。 */
#include "arch/arm64/include/mm.h"
#include "kernel/include/console.h"

static u64 s2_pgd[512]     __attribute__((aligned(4096)));
static u64 s2_l2_dev[512]  __attribute__((aligned(4096)));
static u64 s2_l3_uart[512] __attribute__((aligned(4096)));
static u64 s2_l2_ram[512]  __attribute__((aligned(4096)));

/* 表描述符: valid|table + next table addr[51:12] */
#define DSC_TABLE(a)      (((u64)(a) & ~0xFFFULL) | 0x3UL)
/* 页/块描述符(DDI 0487 stage-2): addr | MemAttr[5:2] | S2AP[7:6]=RW
 *   | AF(bit10)=1 — AF 必置位,否则首访即 access-flag fault(门禁实测)
 *   | SH[9:8]=0 */
#define DSC_MAP(a, attr)  (((u64)(a) & ~0xFFFULL) | ((attr) << 2) | (0x3UL << 6) | (0x1UL << 10) | 0x1UL)
/* L3 页描述符 bit1=1 */
#define DSC_PAGE(a, attr) (DSC_MAP((a), (attr)) | 0x2UL)
/* XN[1:0] (bits 54:53): EL0/EL1 均不可执行 */
#define DSC_XN            (0x3UL << 53)

void stage2_init(void)
{
    /* guest RAM: L1[1] → l2_ram; L2[0] = 2MB block @ 0x50000000(非恒等) */
    s2_pgd[0x40000000UL >> 30] = DSC_TABLE(s2_l2_ram);
    s2_l2_ram[0] = DSC_MAP(0x50000000UL, S2_ATTR_NORMAL);

    /* guest UART: L1[0] → l2_dev; L2[0x09000000>>21] → l3_uart; L3[0] = 4KB page */
    s2_pgd[0] = DSC_TABLE(s2_l2_dev);
    s2_l2_dev[0x09000000UL >> 21] = DSC_TABLE(s2_l3_uart);
    s2_l3_uart[0] = DSC_PAGE(0x09000000UL, S2_ATTR_DEVICE) | DSC_XN;

    __asm__ volatile("dsb sy");
}

u64 *stage2_pgd(void)
{
    return s2_pgd;
}

int stage2_walk(u64 ipa, u64 *pa)
{
    u64 d = s2_pgd[(ipa >> 30) & 0x1FF];
    u64 *l2, *l3;

    if ((d & 0x3) != 0x3UL)
        return -1;                                  /* 本布局 L1 无 block */
    l2 = (u64 *)(d & ~0xFFFULL);
    d = l2[(ipa >> 21) & 0x1FF];
    if ((d & 0x1) == 0)
        return -1;                                  /* 未映射 */
    if ((d & 0x2) == 0) {                           /* L2 block: 2MB 粒 */
        *pa = (d & ~0x1FFFFFULL) | (ipa & 0x1FFFFF);
        return 0;
    }
    l3 = (u64 *)(d & ~0xFFFULL);
    d = l3[(ipa >> 12) & 0x1FF];
    if ((d & 0x3) != 0x3UL)
        return -1;
    *pa = (d & ~0xFFFULL) | (ipa & 0xFFF);
    return 0;
}

static void dump_table(const char *name, u64 *t)
{
    u32 i;

    for (i = 0; i < 512; i++) {
        if (!t[i])
            continue;
        console_puts("  ");
        console_puts(name);
        console_puts("[");
        console_puthex(i);
        console_puts("] = 0x");
        console_puthex(t[i]);
        console_puts("\n");
    }
}

void stage2_dump(void)
{
    console_puts("stage-2 tables:\n");
    console_puts("  IPA 0x40000000 (2MB block) -> PA 0x50000000  # guest RAM, non-identity\n");
    console_puts("  IPA 0x09000000 (4KB page)   -> PA 0x09000000  # guest UART, device nGnRE\n");
    dump_table("pgd",     s2_pgd);
    dump_table("l2_dev",  s2_l2_dev);
    dump_table("l3_uart", s2_l3_uart);
    dump_table("l2_ram",  s2_l2_ram);
}

#define BLK_2M  0x200000UL
#define PG_4K   0x1000UL

/* ---- 27.04 第二站:per-VM 构建器 ----
 * RAM 走 l2_ram 2MB block,设备页走 l2_dev→l3_dev 4KB page(强制 XN)。
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

        if (d->size != PG_4K || (d->ipa & (PG_4K - 1)) || (d->pa & (PG_4K - 1)))
            return -1;
        if (d->ipa >= 0x40000000UL)
            return -1;                       /* 设备段必须在 IPA 0-1GB 内 */
        sp->l2_dev[l2i] = DSC_TABLE(sp->l3_dev);
        sp->l3_dev[(d->ipa >> 12) & 0x1FF] = DSC_PAGE(d->pa, d->attr) | DSC_XN;
    }

    sp->vttbr = (u64)sp->pgd | ((vmid & 0xFFUL) << 48);
    return 0;
}
