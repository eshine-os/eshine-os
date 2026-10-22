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
    ppi_configure((1u << GIC_PPI_HPTIMER) | (1u << GIC_PPI_VTIMER));

    /* CPU interface:sysreg 直访 + 全优先级放行 + EOImode=1(EOIR 只降优先级) */
    __asm__ volatile("msr hcr_el2, %0" :: "r"(HCR_EL2_HOST)); /* 见 gicv3.h */
    __asm__ volatile("msr icc_sre_el2, %0" :: "r"(3UL));   /* SRE + 允许 EL1 访问 */
    __asm__ volatile("isb");
    __asm__ volatile("msr icc_pmr_el1, %0" :: "r"(0xFFUL));
    __asm__ volatile("msr icc_ctlr_el1, %0" :: "r"(2UL));  /* EOImode=1 */
    __asm__ volatile("msr icc_igrpen1_el1, %0" :: "r"(1UL));
    __asm__ volatile("msr cnthctl_el2, %0" :: "r"(3UL));   /* EL1 计数器访问位开 */
    __asm__ volatile("isb");
    wr32(GICD_BASE + GICD_CTLR, GICD_CTLR_EN_GRP1NS);  /* 开 Group1(NS) 分发 */
    vgic_enable();                                    /* B 相:虚拟面同场就绪 */
}

/* ---- B 相:虚拟 CPU interface(ICH_*),guest 经 ICV 视图消费 ---- */

#define ICH_VMCR_VENG1   (1UL << 1)      /* 虚拟 Group1 使能(guest 的 IGRPEN1 镜像) */
#define ICH_LR_GROUP1    (1UL << 60)
#define ICH_LR_PENDING   (1UL << 61)
#define ICH_LR_HW        (1UL << 62)

void vgic_enable(void)
{
    /* 虚拟优先级全放行 + Group1 使能;LR 空载,注入时刻才装填。
     * EOImode 用复位默认 0:guest 一发 EOIR 即降优先级+撤销,硬件联动撤物理线。 */
    __asm__ volatile("msr ich_vmcr_el2, %0"
                     :: "r"((0xFFUL << 24) | ICH_VMCR_VENG1));
    __asm__ volatile("msr ich_hcr_el2, %0" :: "r"(1UL));   /* 虚拟面开(En) */
    __asm__ volatile("isb");
}

void vgic_inject_vtimer(void)
{
    /* HW 映射:vINTID=27(guest CNTV 视角),pINTID=27(物理同号线)。
     * guest 虚 EOI(EOIR) -> 硬件撤销物理 27 的 active;
     * 宿主对物理 27 只降优先级不 DIR,期间电平线被 active 压住不再重挂起。 */
    u64 lr = GIC_PPI_VTIMER | (GIC_PPI_VTIMER << 32)   /* [31:0]v / [47:32]p */
           | ICH_LR_GROUP1 | ICH_LR_PENDING | ICH_LR_HW;

    __asm__ volatile("msr ich_lr0_el2, %0" :: "r"(lr));
    __asm__ volatile("isb");
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
