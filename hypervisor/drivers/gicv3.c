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
static void ppi_configure(u64 sgi_base, u32 mask)
{
    u32 group = 0;
    u32 id;

    wr32(sgi_base + GICR_ICENABLER0, mask);    /* 已知态:先关 */
    wr32(sgi_base + GICR_ICPENDR0, mask);       /* 清挂起 */
    wr32(sgi_base + GICR_ICACTIVER0, mask);    /* 清活动 */
    for (id = 16; id < 32; id++) {
        if (!(mask & (1u << id)))
            continue;
        group |= 1u << id;                          /* Group 1(NS) */
        {
            u64 addr = sgi_base + GICR_IPRIORITYR(id);
            u32 shift = 8 * (id & 3);
            u32 prio = rd32(addr);

            prio = (prio & ~(0xFFu << shift)) | (0x00u << shift);   /* 最高优先级 */
            wr32(addr, prio);
        }
    }
    wr32(sgi_base + GICR_IGROUPR0, group);
    wr32(sgi_base + GICR_ISENABLER0, mask);
}

void gicv3_init(void)
{
    (void)rd32(GICR_RD_BASE + GICR_TYPER);          /* CPU0 redistributor 在位探测 */
    wr32(GICD_BASE + GICD_CTLR, 0);                 /* 配置期先关 */
    wr32(GICR_RD_BASE + GICR_WAKER, 0);             /* ProcessorSleep=0 */
    ppi_configure(GICR_SGI_BASE,
                  (1u << GIC_PPI_HPTIMER) | (1u << GIC_PPI_VTIMER));

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
#define ICH_LR_GROUP1    (1UL << 60)     /* vINTID 组 */
#define ICH_LR_HW        (1UL << 61)     /* HW 映射:vINTID 挂起跟随物理线 */
#define ICH_LR_PENDING   (1UL << 62)     /* State[1]:挂起(缺此位=LR 无效,QEMU 弃投) */

void vgic_enable(void)
{
    /* 虚拟优先级全放行 + Group1 使能;LR 空载,注入时刻才装填。
     * EOImode 用复位默认 0:guest 一发 EOIR 即降优先级+撤销,硬件联动撤物理线。 */
    __asm__ volatile("msr ich_vmcr_el2, %0"
                     :: "r"((0xFFUL << 24) | ICH_VMCR_VENG1));
    __asm__ volatile("msr ich_hcr_el2, %0" :: "r"(1UL));   /* 虚拟面开(En) */
    __asm__ volatile("isb");
}

void vgic_wake_waiter(void)
{
    /* 27 到点的宿主侧动作:关 CNTV 让电平线降(不清则重挂起风暴),
     * 只降优先级不撤物理线——撤线交给 WOKE 后泵注入时 guest 的虚 EOI
     * 硬件联动(LR 不预挂:per-pCPU 单通道,在跑的别的 VM 会串台消费)。 */
    __asm__ volatile("msr cntv_ctl_el0, %0" :: "r"(0UL));
    gicv3_eoi_drop(GIC_PPI_VTIMER);
}

bool vtimer_inflight(void)
{
    /* 站十六:vm27 挂起在途。挂起未认领的 v27 活不过 VM 切换——HW LR
     * 的 pending 由物理线驱动,下一位客装自己的 vtimer 时线一落,该拍
     * 即蒸发(Linux 丢拍冻结的根因;旧 1Hz 慢轮转拍总在同片认领,快
     * 轮转跨片挂起成常态)。抢占决策点守门:在途则就地重填配额不切走;
     * 认领路径零陷阱,vIRQ 进入后几条指令内即消费。vuart/virtio 的
     * 纯虚拟 LR 不挂物理线,不受此律,按 HW 位滤除。 */
    u64 lr;

    __asm__ volatile("mrs %0, ich_lr0_el2" : "=r"(lr));
    return (lr & ICH_LR_HW) && (lr & ICH_LR_PENDING);
}

void vgic_inject_vtimer(u8 prio)
{
    /* HW 映射:vINTID=27(guest CNTV 视角),pINTID=27(物理同号线);
     * Group+HW+pending 三位全置——HW LR 的挂起语义由物理线驱动,EOI
     * 联动撤线。第八站位号正名:bit61=HW,bit62=State(pending),
     * 缺状态位即 LR 无效(QEMU 弃投,实测 uart 首版只置 61 的教训)。
     * 站十四:[55:48] 优先级随 guest 自配(IPRIORITYR 镜像),
     * 硬 0x00 会落进 Linux pseudo-NMI 阈值把 tick 走成 NMI 路径。 */
    u64 lr = GIC_PPI_VTIMER | (GIC_PPI_VTIMER << 32)   /* [31:0]v / [47:32]p */
           | ((u64)(prio & 0xFFu) << 48)              /* [55:48] 优先级 */
           | ICH_LR_GROUP1 | ICH_LR_PENDING | ICH_LR_HW;

    __asm__ volatile("msr ich_lr0_el2, %0" :: "r"(lr));
    __asm__ volatile("isb");
}

void vgic_inject_sgi(u32 intid, u8 prio)
{
    /* 站十九D:SGI 注入,LR2 通道(与 LR0 vtimer-HW、LR1 uart 分道)。
     * 纯虚拟:Group+State 置足,bit61(HW)不置——HW LR 的挂起跟随
     * 物理线,物理侧无源即永不投递(HW 试探版实测教训);guest 虚
     * EOI 即撤。优先级随 guest 自配镜像(同 vtimer 注入纪律)。 */
    u64 lr = (u64)(intid & 0xFu)
           | ((u64)(prio & 0xFFu) << 48)
           | ICH_LR_GROUP1 | ICH_LR_PENDING;

    __asm__ volatile("msr ich_lr2_el2, %0" :: "r"(lr));
    __asm__ volatile("isb");
}

void vgic_inject_uart(void)
{
    /* 纯虚拟 SPI(无 HW 位——HW LR 的挂起跟随物理线,物理侧无源即
     * 永不投递,HW 试探版实测教训):Group+State(pending) 两位置足,
     * bit61(HW)不置。guest 虚 EOI 即撤,无物理线联动。LR1 与 LR0
     * (vtimer HW)分通道,泵注入纪律不变。 */
    u64 lr = GIC_SPI_UART | ICH_LR_GROUP1 | ICH_LR_PENDING;

    __asm__ volatile("msr ich_lr1_el2, %0" :: "r"(lr));
    __asm__ volatile("isb");
}

void vgic_inject_virtio(void)
{
    /* 同 uart:纯虚拟完成中断,Group+State(pending) 两位置足,
     * HW 位不置(HW LR 挂起跟随物理线,物理侧无源)。LR3 通道,
     * vSPI 35 新号(vblk 时代 34 的设备代际区分,用户批准)——
     * LR2 随 vblk 退役空置。 */
    u64 lr = GIC_SPI_VIRTIO | ICH_LR_GROUP1 | ICH_LR_PENDING;

    __asm__ volatile("msr ich_lr3_el2, %0" :: "r"(lr));
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

void gicv3_secondary_init(u32 cpu)
{
    /* 站十八D:次核自配;站十九D 参数化(CPU1/CPU2)——GICD 共享
     * 不动(CPU0 已开,重复写会闪断 CPU0 的中断面);本核重分发帧
     * (RD+SGI 各 64KB,CPU n @ 0x080A0000+n*0x20000);只配 PPI27
     * (vm2 vtimer 生命线;PPI26 不配=专泵无量子);CPU interface/
     * 虚拟面 per-CPU 硬件同款。 */
    u64 rd = GICR_RD_BASE + (u64)cpu * 0x20000UL;

    (void)rd32(rd + GICR_TYPER);              /* 在位探测(与 CPU0 同款) */
    wr32(rd + GICR_WAKER, 0);                 /* ProcessorSleep=0 */
    ppi_configure(rd + 0x10000UL, (1u << GIC_PPI_VTIMER));
    __asm__ volatile("msr hcr_el2, %0" :: "r"(HCR_EL2_HOST));
    __asm__ volatile("msr icc_sre_el2, %0" :: "r"(3UL));
    __asm__ volatile("isb");
    __asm__ volatile("msr icc_pmr_el1, %0" :: "r"(0xFFUL));
    __asm__ volatile("msr icc_ctlr_el1, %0" :: "r"(2UL));  /* EOImode=1 */
    __asm__ volatile("msr icc_igrpen1_el1, %0" :: "r"(1UL));
    __asm__ volatile("msr cnthctl_el2, %0" :: "r"(3UL));
    __asm__ volatile("msr cptr_el2, xzr");   /* 站十六:TFP 归零防御 */
    __asm__ volatile("isb");
    vgic_enable();
}
