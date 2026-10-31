/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#ifndef ESHINE_GICV3_H
#define ESHINE_GICV3_H

#include "kernel/include/types.h"
#include <stdbool.h>

/* QEMU virt GICv3 连线(公开文档,参考实现 qemu_virt dtsi 同源) */
#define GICD_BASE       0x08000000UL
#define GICR_RD_BASE    0x080A0000UL     /* CPU0 redistributor RD 帧 */
#define GICR_SGI_BASE   0x080B0000UL     /* CPU0 redistributor SGI/PPI 帧 */

/* PPI 接线以活体探针为准(QEMU 8.2.2 -M virt,virtualization=on,gic-version=3,
 * cortex-a57): CNTHP→26(挂起位与到期相关),CNTV→27(认领值 0x1b 实测)。
 * 均为高电平触发,不碰 ICFGR。 */
#define GIC_PPI_HPTIMER 26UL              /* 宿主 CNTHP 物理定时器 */
#define GIC_PPI_VTIMER  27UL              /* guest CNTV 虚拟定时器 */
#define GIC_SPI_UART     33UL              /* QEMU virt PL011 IRQ 线(第八站虚注) */
#define GIC_SPI_VIRTIO   35UL              /* virtio 完成中断(第十站虚注;34 为 vblk 时代遗号) */
#define IAR_SPURIOUS    1023UL

/* HCR_EL2 宿主值:IMO(bit4) 是 EL2 物理 IRQ 的路由门(QEMU target_el_table
 * rw=1 列经此位才回 EL2);guest 期也置此位(EL1 ICC 访问重定向到 ICV)。
 * vcpu_exit 恢复宿主态时必须回此值,不可清零。 */
#define HCR_EL2_HOST 0x10UL

void gicv3_init(void);
void gicv3_secondary_init(u32 cpu);   /* 站19D:帧号=本核 */   /* 站十八D:CPU1 本核面(GICD 共享不动) */                    /* dist/redist/cpuif,EOImode=1 */
u64  gicv3_claim(void);                   /* mrs ICC_IAR1_EL1 */
void gicv3_eoi_dir(u64 intid);            /* 降优先级 + DIR 撤销(宿主自有) */
void gicv3_eoi_drop(u64 intid);           /* 只降优先级(27 的物理线由 guest 虚 EOI 联动撤销) */
u64  vgic_make_vtimer_lr(u8 prio);       /* 站28:vtimer LR 值构造(硬注与帧直写同源) */
void vgic_enable(void);                    /* B 相:ICH_VMCR/ICH_HCR 虚拟面开 */
void vgic_inject_vtimer(u8 prio);
bool vtimer_inflight(void);        /* 站十六:vm27 挂起在途(抢占守门) */         /* B 相:LR0 HW 映射注入 vINTID 27 */
void vgic_inject_uart(void);
void vgic_inject_sgi(u32 intid, u8 prio);   /* 站19D:LR2 通道 */              /* 第八站:LR1 纯虚拟注入 vINTID 33 */
void vgic_inject_virtio(void);             /* 第十站:LR3 纯虚拟注入 vINTID 35 */
void vgic_wake_waiter(void);              /* 27 到点:关 CNTV 降线+降优先级(WOKE 交泵注入) */

#endif /* ESHINE_GICV3_H */
