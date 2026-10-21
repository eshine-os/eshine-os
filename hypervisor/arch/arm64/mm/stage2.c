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
/* 页/块描述符: addr | MemAttr[5:2] | S2AP[8:7]=RW | valid */
#define DSC_MAP(a, attr)  (((u64)(a) & ~0xFFFULL) | ((attr) << 2) | (0x3UL << 7) | 0x1UL)
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
