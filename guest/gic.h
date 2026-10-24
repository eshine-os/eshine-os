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
#define GICD_IGROUPR0   0x080u            /* SPI 面:寄存器 0 覆盖 INTID 32-63 */
#define GICD_ISENABLER0 0x100u
#define GICD_PIDR2      0xFFE8u

#define GICR_RD_CTLR    0x000u
#define GICR_RD_TYPER   0x008u
#define GICR_RD_WAKER   0x014u
#define GICR_SGI        0x10000u         /* SGI 帧:INTID 0-31 */
#define GICR_IGROUPR0   0x080u           /* 以下均为 SGI 帧内偏移 */
#define GICR_ISENABLER0 0x100u

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
        puts_uart("gic FAIL: not last\n");   /* 单帧单核:Last 位 */
        return;
    }
    puts_uart("gicd v3 ok\n");

    vgicd_write(GICD_CTLR, 1u);              /* 使能 distributor */
    vgicr_write(GICR_RD_CTLR, 1u);
    vgicr_write(GICR_RD_WAKER, 0u);          /* 清 ProcessorSleep(轮询即过) */
    (void)vgicr_read(GICR_RD_WAKER);

    vgicr_write(GICR_SGI + GICR_IGROUPR0, 0xFFFFFFFFu);   /* 全组 1 */
    vgicr_write(GICR_SGI + GICR_ISENABLER0, 1u << 27);    /* PPI 27 vtimer */
    vgicd_write(GICD_IGROUPR0, 0xFFFFFFFFu);
    vgicd_write(GICD_ISENABLER0, (1u << 1) | (1u << 3));  /* SPI 33+35 */

    /* 回读验证:写经陷阱入模型,读再经陷阱出模型——使能真的落了地 */
    if ((vgicr_read(GICR_SGI + GICR_ISENABLER0) & (1u << 27)) == 0u) {
        puts_uart("gic FAIL: ppi27\n");
        return;
    }
    if (vgicd_read(GICD_ISENABLER0) != ((1u << 1) | (1u << 3))) {
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
