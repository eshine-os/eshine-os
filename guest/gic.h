/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#ifndef GUEST_GIC_H
#define GUEST_GIC_H

typedef unsigned long long u64;   /* 与 hvc.h 同型,C11 允许重复定义 */

/* guest 视角的 GIC:HCR.IMO=1 使 EL1 的 ICC_* 访问重定向到 ICV(虚拟面),
 * 代码按普通 GIC 语义写,重定向由硬件完成。本板 CNTV 虚拟定时器=INTID 27
 * (宿主 HW-LR 注入同号 vINTID)。EOImode 用复位默认 0:EOIR 一发即降+撤。 */
#define GUEST_INTID_VTIMER   27UL
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

#endif /* GUEST_GIC_H */
