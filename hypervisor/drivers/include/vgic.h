/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#ifndef ESHINE_VGIC_H
#define ESHINE_VGIC_H

#include <stdbool.h>
#include "kernel/include/types.h"

/* 第十二站:vGIC distributor/redistributor 仿真面(中断面 OS 化)。
 * 形态:真启动/使能路径有真语义(CTLR/TYPER/PIDR2/WAKER/ISENABLER),
 * 其余存储型;未知寄存器 RAZ/WI(设备级默认拒绝,承 vblk 先例)。
 * 泵注入三通道(vtimer 27/vuart 33/virtio 35)改按 guest 使能位
 * 门控——distributor 成为注入权威:guest 没使能就不注入,也不代
 * 完成 WFI(guest 自己的配置自己负责)。
 * 状态随 VM 生命周期:vm_create 复位,不跨 create(与磁盘持久化
 * 互补——distributor 是每 VM 新硬件,磁盘是每 VM 旧资产)。
 * offset 均以 ARM 手册为准(IHI0069,经知识库复核):
 *   GICD: CTLR 0x000/TYPER 0x004/IGROUPR 0x080+4n/ISENABLER 0x100+4n
 *         (SPI 面:寄存器 n 覆盖 INTID 32(n+1)..32(n+2)-1,SGI/PPI
 *          在 GICR)/IPRIORITYR 0x400+4n/ICFGR 0xC00+4n/PIDR2 0xFFE8
 *   GICR RD: CTLR 0x000/IIDR 0x004/TYPER 0x008(64bit)/WAKER 0x014
 *   GICR SGI(+0x10000): IGROUPR0 0x080/ISENABLER0 0x100/ICENABLER0
 *         0x180/ISPENDR0 0x200/ICPENDR0 0x280/ISACTIVER0 0x300/
 *         ICACTIVER0 0x380/IPRIORITYR0 0x400/ICFGR0 0xC00/ICFGR1 0xC04 */

#define VGIC_SPI_WORDS 1     /* ISENABLER 字数:覆盖 INTID 32-63(通道 33/35) */
#define VGIC_SGI_BYTES 32    /* SGI 面优先级字节:INTID 0-31 */

struct vgic {
    /* GICD 面(SPI:INTID 32 起) */
    u32 dist_ctlr;
    u32 dist_igroupr[VGIC_SPI_WORDS];
    u32 dist_isenabler[VGIC_SPI_WORDS];   /* ICENABLER 读即回此值 */
    u32 dist_ipendr[VGIC_SPI_WORDS];
    u32 dist_iactiver[VGIC_SPI_WORDS];
    u8  dist_ipriorityr[32 * VGIC_SPI_WORDS];
    u32 dist_icfgr[VGIC_SPI_WORDS];
    /* GICR RD 面 */
    u32 rd_ctlr;
    u32 rd_waker;
    /* GICR SGI 面(INTID 0-31) */
    u32 sgi_igroupr0;
    u32 sgi_isenabler0;
    u32 sgi_ipendr0;
    u32 sgi_iactiver0;
    u8  sgi_ipriorityr[VGIC_SGI_BYTES];
    u32 sgi_icfgr0;                     /* SGI:1 bit/INTID */
    u32 sgi_icfgr1;                     /* PPI:2 bit/INTID,vtimer 27=level(bit55:54=00) */
};

void vgic_reset(struct vgic *g);
u64  vgic_dist_mmio_read(struct vgic *g, u32 off, u32 size);
void vgic_dist_mmio_write(struct vgic *g, u32 off, u64 val, u32 size);
u64  vgic_redist_mmio_read(struct vgic *g, u32 off, u32 size);
void vgic_redist_mmio_write(struct vgic *g, u32 off, u64 val, u32 size);

/* 泵注入门控:distributor 是权威(guest 使能了才注入) */
bool vgic_spi_enabled(const struct vgic *g, u32 intid);   /* INTID 32-63 */
bool vgic_ppi_enabled(const struct vgic *g, u32 intid);   /* INTID 0-31 */

#endif /* ESHINE_VGIC_H */
