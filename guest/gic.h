/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#ifndef GUEST_GIC_H
#define GUEST_GIC_H

typedef unsigned long long u64;   /* 与 hvc.h 同型,C11 允许重复定义 */

/* guest 视角的 GIC:HCR.IMO=1 使 EL1 的 ICC_* 访问重定向到 ICV(虚拟面),
 * 代码按普通 GIC 语义写,重定向由硬件完成。本板 CNTV 虚拟定时器=INTID 27
 * (宿主 HW-LR 注入同号 vINTID)。EOImode 用复位默认 0:EOIR 一发即降+撤。 */
#define GUEST_INTID_VTIMER   27UL
#define GUEST_INTID_UART     33UL   /* 宿主 vuart 虚注(QEMU virt PL011 线) */
#define GUEST_INTID_VIRTIO   35UL   /* 宿主 virtio 完成虚注(第十站;34 为 vblk 遗号) */
#define GUEST_INTID_IPI      0UL    /* 站二十:SGI 0 = RTOS 跨核重调度 */
#define GUEST_INTID_SPURIOUS 1023UL

static inline void gic_guest_enable(void)
{
    __asm__ volatile("msr icc_pmr_el1, %0" :: "r"(0xFFUL));
    __asm__ volatile("msr icc_igrpen1_el1, %0" :: "r"(1UL));
    __asm__ volatile("msr daifclr, #2");   /* 解 IRQ:中断面就绪后才可 */
}

static inline u64 gic_claim(void)
{
    u64 v;

    __asm__ volatile("mrs %0, icc_iar1_el1" : "=r"(v));
    return v;
}

static inline void gic_eoir(u64 intid)
{
    __asm__ volatile("msr icc_eoir1_el1, %0" :: "r"(intid));
}

/* ---- 第十二站:vGIC distributor/redistributor MMIO 面 ----
 * 真启动式初始化:经 MMIO 探测(PIDR2)/开 CTLR/清 WAKER 睡眠/
 * ISENABLER 逐 INTID 使能——distributor 从此是注入权威(宿主泵按
 * 使能位门控)。访存器同 miniuart 契约:volatile asm 保证 ISV=1。
 * offset 与宿主 drivers/vgic.c 一表(guest 视角 IPA 窗口)。 */

#define VGICD_BASE 0x08000000UL           /* 与树 intc@8000000 对表 */
#define VGICR_BASE 0x080A0000UL           /* RD 帧;SGI 帧 +0x10000 */

#define GICD_CTLR       0x000u
#define GICD_TYPER      0x004u
/* 站十四规范对齐(IHI0069):GICD 家族 word0(INTID 0-31)属重分发器
 * 银行视图,guest 不从 GICD 动;SPI 32-63 在 word1——旧站曾以
 * "寄存器 0 覆盖 32-63"自造约定,与宿主 vgic 译码同步对齐规范。 */
#define GICD_IGROUPR1   0x084u            /* SPI 32-63 组 */
#define GICD_ISENABLER1 0x104u            /* SPI 32-63 使能 */
#define GICD_PIDR2      0xFFE8u

#define GICR_RD_CTLR    0x000u
#define GICR_RD_TYPER   0x008u
#define GICR_RD_WAKER   0x014u
#define GICR_SGI        0x10000u         /* SGI 帧:INTID 0-31 */
#define GICR_IGROUPR0   0x080u           /* 以下均为 SGI 帧内偏移 */
#define GICR_ISENABLER0 0x100u

/* 站二十:次核重分发帧初始化(帧 1 @ VGICR_BASE+0x20000)。
 * GICR 是 per-CPU 面——主核 gic_dist_init 只开了帧 0;次核不开
 * 自家帧的 SGI0/PPI27,宿主 vgic_sgi_take 的 isenabler 门即滤。
 * rx=0 定谳是三层链:MMIO 窗口须 0x40000(vm.c devices[3] 随
 * n_vcpu)→ 模型帧数须 2(vgic_reset 随 n_vcpu,帧卫兵 WI 症)
 * → 本使能位。只动本帧三寄存器,distributor 侧主核已配,不重写。 */
static inline void gic_secondary_frame_init(unsigned int cpu)
{
    unsigned long base = VGICR_BASE + 0x20000UL * cpu;

    /* 直接 volatile str(与 vgicr_write 同形):文件序上 vgicr_write
     * 在本函数之后,自包含免依赖。短临界段与主核 gic_dist_init 同律。 */
    __asm__ volatile("msr daifset, #2");
    __asm__ volatile("str %w0, [%1]" :: "r"(0u),
                     "r"(base + GICR_RD_WAKER) : "memory");
    __asm__ volatile("str %w0, [%1]" :: "r"(0xFFFFFFFFu),
                     "r"(base + GICR_SGI + GICR_IGROUPR0) : "memory");
    __asm__ volatile("str %w0, [%1]" :: "r"((1u << 27) | 1u),
                     "r"(base + GICR_SGI + GICR_ISENABLER0) : "memory");
    __asm__ volatile("msr daifclr, #2");   /* PPI27 vtimer + SGI0(IPI) */
}

static inline unsigned int vgicd_read(unsigned long off)
{
    unsigned long addr = VGICD_BASE + off;
    unsigned int v;

    __asm__ volatile("ldr %w0, [%1]" : "=r"(v) : "r"(addr));
    return v;
}

static inline void vgicd_write(unsigned long off, unsigned int v)
{
    unsigned long addr = VGICD_BASE + off;

    __asm__ volatile("str %w0, [%1]" :: "r"(v), "r"(addr) : "memory");
}

static inline unsigned int vgicr_read(unsigned long off)
{
    unsigned long addr = VGICR_BASE + off;
    unsigned int v;

    __asm__ volatile("ldr %w0, [%1]" : "=r"(v) : "r"(addr));
    return v;
}

static inline void vgicr_write(unsigned long off, unsigned int v)
{
    unsigned long addr = VGICR_BASE + off;

    __asm__ volatile("str %w0, [%1]" :: "r"(v), "r"(addr) : "memory");
}

/* 真启动式 GIC bring-up(判据自报两行,门禁 [12-13]):
 *   gicd v3 ok       <- PIDR2 探测 [7:4]==3
 *   gic on: 27 33 35 <- GICR PPI 27 + GICD SPI 33/35 使能并回读验证 */
static void gic_dist_init(void)
{
    if ((vgicd_read(GICD_PIDR2) & 0xF0u) != 0x30u) {
        puts_uart("gic FAIL: not v3\n");
        return;
    }
    if ((vgicd_read(GICD_TYPER) & 0x1Fu) == 0u) {
        puts_uart("gic FAIL: no spi\n");
        return;
    }
    if ((vgicr_read(GICR_RD_TYPER) & 0x10u) == 0u) {
        /* 站二十:双帧视图(vm0 双核)——帧 0 非末帧(Last=0)不再是
         * 故障;转证帧 1:窗口身份(PIDR2=0x30)+末帧 Last=1。窗口
         * 缺位/帧数没跟上当场炸,不留给次核入场才现形。vm1 单核
         * 对照帧 0 即末帧(Last=1),旧判据原样通过。 */
        if (vgicr_read(0x20000u + 0xFFE8u) != 0x30u ||
            (vgicr_read(0x20000u + GICR_RD_TYPER) & 0x10u) == 0u) {
            puts_uart("gic FAIL: frame1\n");
            return;
        }
    }
    puts_uart("gicd v3 ok\n");

    vgicd_write(GICD_CTLR, 1u);              /* 使能 distributor */
    vgicr_write(GICR_RD_CTLR, 1u);
    vgicr_write(GICR_RD_WAKER, 0u);          /* 清 ProcessorSleep(轮询即过) */
    (void)vgicr_read(GICR_RD_WAKER);

    vgicr_write(GICR_SGI + GICR_IGROUPR0, 0xFFFFFFFFu);   /* 全组 1 */
    vgicr_write(GICR_SGI + GICR_ISENABLER0, (1u << 27) | 1u);
    /* PPI 27 vtimer + 站二十 SGI 0(跨核重调度 IPI)。不使能则宿主
     * vgic_sgi_take 的 isenabler 门滤掉,ipidemo rx=0 实测定谳。 */
    vgicd_write(GICD_IGROUPR1, 0xFFFFFFFFu);
    vgicd_write(GICD_ISENABLER1, (1u << 1) | (1u << 3)); /* SPI 33+35 */

    /* 回读验证:写经陷阱入模型,读再经陷阱出模型——使能真的落了地 */
    if ((vgicr_read(GICR_SGI + GICR_ISENABLER0) &
         ((1u << 27) | 1u)) != ((1u << 27) | 1u)) {
        puts_uart("gic FAIL: ppi27\n");
        return;
    }
    if (vgicd_read(GICD_ISENABLER1) != ((1u << 1) | (1u << 3))) {
        puts_uart("gic FAIL: spi\n");
        return;
    }
    if (vgicr_read(GICR_RD_WAKER) != 0u) {
        puts_uart("gic FAIL: waker\n");
        return;
    }
    puts_uart("gic on: 27 33 35\n");
}

#endif /* GUEST_GIC_H */
