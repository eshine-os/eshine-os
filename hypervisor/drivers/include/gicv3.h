/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#ifndef ESHINE_GICV3_H
#define ESHINE_GICV3_H

#include "kernel/include/types.h"

/* QEMU virt GICv3 连线(公开文档,参考实现 qemu_virt dtsi 同源) */
#define GICD_BASE       0x08000000UL
#define GICR_RD_BASE    0x080A0000UL     /* CPU0 redistributor RD 帧 */
#define GICR_SGI_BASE   0x080B0000UL     /* CPU0 redistributor SGI/PPI 帧 */

/* PPI 接线以活体探针为准(QEMU 8.2.2 -M virt,virtualization=on,gic-version=3,
 * cortex-a57): 使能 26/27 各挂起位与 CNTHP 到期时刻相关,实测 CNTHP→26;
 * CNTV(guest vtimer,B 相)待同法实测。均为高电平触发。 */
#define GIC_PPI_HPTIMER 26UL              /* 宿主 CNTHP 物理定时器(实测) */
#define GIC_PPI_VTIMER  29UL              /* guest CNTV(B 相启用,待实测) */
#define IAR_SPURIOUS    1023UL

void gicv3_init(void);                    /* dist/redist/cpuif,EOImode=1 */
u64  gicv3_claim(void);                   /* mrs ICC_IAR1_EL1 */
void gicv3_eoi_dir(u64 intid);            /* 降优先级 + DIR 撤销(宿主自有) */
void gicv3_eoi_drop(u64 intid);           /* 只降优先级(27 的物理线由 guest 虚 EOI 联动撤销) */

#endif /* ESHINE_GICV3_H */
