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

#define VGIC_MAX_FRAMES 2                 /* 站19D:每 VM 至多 2 vCPU 帧 */

struct vgic {
    /* GICD 面(SPI:INTID 32 起) */
    u32 dist_ctlr;
    u32 dist_igroupr[VGIC_SPI_WORDS];
    u32 dist_isenabler[VGIC_SPI_WORDS];   /* ICENABLER 读即回此值 */
    u32 dist_ipendr[VGIC_SPI_WORDS];
    u32 dist_iactiver[VGIC_SPI_WORDS];
    u8  dist_ipriorityr[32 * VGIC_SPI_WORDS];
    u32 dist_icfgr[VGIC_SPI_WORDS];
    /* 站十九D:RD/SGI 面 per-vCPU 帧(帧号=off>>17;单 vCPU VM 只有
     * 帧 0 在用,nframes=1——Linux 的重分发帧发现按 TYPER 亲和
     * (Aff0=帧号,位[39:32],MPIDR 同构;基准 gic_v3.c 实证
     * cpu_mpidr=typer>>32)+ Last(末帧)逐帧匹配)。 */
    u32 nframes;
    u32 rd_ctlr[VGIC_MAX_FRAMES];
    u32 rd_waker[VGIC_MAX_FRAMES];
    /* GICR SGI 面(INTID 0-31)per 帧 */
    u32 sgi_igroupr0[VGIC_MAX_FRAMES];
    u32 sgi_isenabler0[VGIC_MAX_FRAMES];
    u32 sgi_ipendr0[VGIC_MAX_FRAMES];
    u32 sgi_iactiver0[VGIC_MAX_FRAMES];
    u8  sgi_ipriorityr[VGIC_MAX_FRAMES][VGIC_SGI_BYTES];
    u32 sgi_icfgr0[VGIC_MAX_FRAMES];   /* SGI:1 bit/INTID */
    u32 sgi_icfgr1[VGIC_MAX_FRAMES];   /* PPI:2 bit/INTID,vtimer 27=level */
};

void vgic_reset(struct vgic *g, u32 nframes);   /* 站19D:面随 VM 的 vCPU 数 */
u64  vgic_dist_mmio_read(struct vgic *g, u32 off, u32 size);
void vgic_dist_mmio_write(struct vgic *g, u32 off, u64 val, u32 size);
u64  vgic_redist_mmio_read(struct vgic *g, u32 frame, u32 off, u32 size);
void vgic_redist_mmio_write(struct vgic *g, u32 frame, u32 off, u64 val,
                            u32 size);

/* 泵注入门控:distributor 是权威(guest 使能了才注入) */
bool vgic_spi_enabled(const struct vgic *g, u32 intid);   /* INTID 32-63 */
bool vgic_ppi_enabled(const struct vgic *g, u32 intid);   /* INTID 0-31 */
u8    vgic_ppi_prio(const struct vgic *g, u32 intid);     /* 注入 LR 优先级镜像 */

/* 站十九D:SGI 面(guest 的 IPI 通道)。SGIR 写经 GICD 解码目标帧置
 * 挂起;泵入场按帧取走(最低位且已使能者)注 LR2(纯虚拟),
 * 挂起位取走即清——与 uart 同律,LR 单通道一次一条。 */
bool vgic_sgi_take(struct vgic *g, u32 frame, u32 *intid, u8 *prio);
u32   vgic_sgi1r_write(struct vgic *g, u64 val);  /* 站19D:EL2 截获转发,
                                                    * 返回命中帧掩码 */
bool  vgic_sgi_pending(const struct vgic *g, u32 frame); /* 站20:停泊复查
                                                    * 判据——发送窗落在
                                                    * 目标跑动期、目标又
                                                    * 先停泊后进场的竞态,
                                                    * IPI 搁浅在软件挂起位
                                                    * 无人取(实测 csd 锁
                                                    * 死锁冻靴) */

#endif /* ESHINE_VGIC_H */
