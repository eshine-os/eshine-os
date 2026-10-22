/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
/* GICv3 驱动:物理面。偏移按 IHI0069;PPI 配置在 SGI 帧。 */
#include "drivers/include/gicv3.h"

static inline void wr32(u64 addr, u32 v)  { *(volatile u32 *)addr = v; }
static inline u32  rd32(u64 addr)          { return *(volatile u32 *)addr; }

#define GICD_CTLR            0x000
#define GICD_CTLR_EN_GRP1NS  (1u << 1)   /* DS=1 时仅 bit0/1 可写 = 两个组分发开关 */
#define GICR_TYPER          0x008
#define GICR_WAKER          0x014
#define GICR_IGROUPR0       0x080        /* SGI 帧偏移为 GICD 家族镜像(IHI0069 表 8-2) */
#define GICR_ISENABLER0     0x100
#define GICR_ICENABLER0     0x180
#define GICR_ICPENDR0       0x280
#define GICR_ICACTIVER0     0x380
#define GICR_IPRIORITYR(n)  (0x400 + 4 * ((n) >> 2))   /* 每 intid 一字节 */

/* PPI 组/优先级/使能 —— mask 的 bit n = intid n(仅 16-31 有效)。
 * 先禁用并清 pending/active 到已知态,再配组与优先级,最后才使能;
 * 不碰 ICFGR:定时器类 PPI 复位即电平触发,是硬件默认语义。 */
static void ppi_configure(u32 mask)
{
    u32 group = 0;
    u32 id;

    wr32(GICR_SGI_BASE + GICR_ICENABLER0, mask);    /* 已知态:先关 */
    wr32(GICR_SGI_BASE + GICR_ICPENDR0, mask);       /* 清挂起 */
    wr32(GICR_SGI_BASE + GICR_ICACTIVER0, mask);    /* 清活动 */
    for (id = 16; id < 32; id++) {
        if (!(mask & (1u << id)))
            continue;
        group |= 1u << id;                          /* Group 1(NS) */
        {
            u64 addr = GICR_SGI_BASE + GICR_IPRIORITYR(id);
            u32 shift = 8 * (id & 3);
            u32 prio = rd32(addr);

            prio = (prio & ~(0xFFu << shift)) | (0x00u << shift);   /* 最高优先级 */
            wr32(addr, prio);
        }
    }
    wr32(GICR_SGI_BASE + GICR_IGROUPR0, group);
    wr32(GICR_SGI_BASE + GICR_ISENABLER0, mask);
}

void gicv3_init(void)
{
    (void)rd32(GICR_RD_BASE + GICR_TYPER);          /* CPU0 redistributor 在位探测 */
    wr32(GICD_BASE + GICD_CTLR, 0);                 /* 配置期先关 */
    wr32(GICR_RD_BASE + GICR_WAKER, 0);             /* ProcessorSleep=0 */
    ppi_configure(1u << GIC_PPI_HPTIMER);          /* B 相扩为 | 1u<<GIC_PPI_VTIMER */

    /* CPU interface:sysreg 直访 + 全优先级放行 + EOImode=1(EOIR 只降优先级) */
    __asm__ volatile("msr hcr_el2, %0" :: "r"(0x10UL)); /* IMO(bit4):EL2 物理 IRQ
        * 路由门(QEMU target_el_table rw=1 列必须 hcr=1 才回 EL2;参考实现同值);
        * 真硬件语义为 EL1 以下 ICC 重定向,B 相 guest 亦需;EL2 自身不受影响 */
    __asm__ volatile("msr icc_sre_el2, %0" :: "r"(3UL));   /* SRE + 允许 EL1 访问 */
    __asm__ volatile("isb");
    __asm__ volatile("msr icc_pmr_el1, %0" :: "r"(0xFFUL));
    __asm__ volatile("msr icc_ctlr_el1, %0" :: "r"(2UL));  /* EOImode=1 */
    __asm__ volatile("msr icc_igrpen1_el1, %0" :: "r"(1UL));
    __asm__ volatile("msr cnthctl_el2, %0" :: "r"(3UL));   /* EL1 计数器访问位开 */
    __asm__ volatile("isb");
    wr32(GICD_BASE + GICD_CTLR, GICD_CTLR_EN_GRP1NS);  /* 开 Group1(NS) 分发 */
}

u64 gicv3_claim(void)
{
    u64 v;

    __asm__ volatile("mrs %0, icc_iar1_el1" : "=r"(v));
    return v;
}

void gicv3_eoi_dir(u64 intid)
{
    __asm__ volatile("msr icc_eoir1_el1, %0" :: "r"(intid));
    __asm__ volatile("msr icc_dir_el1, %0" :: "r"(intid));
}

void gicv3_eoi_drop(u64 intid)
{
    __asm__ volatile("msr icc_eoir1_el1, %0" :: "r"(intid));
}
