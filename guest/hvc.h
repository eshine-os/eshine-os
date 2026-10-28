/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#ifndef GUEST_HVC_H
#define GUEST_HVC_H

typedef unsigned long long u64;

/* hypercall ABI(第六站后仅控制类):#1 exit / #2 yield / #4 host 交还
 * RX / #5 fg 只读查询(站20 心跳门控)。x0 入参/返回,x1-x17 全部
 * 易失(EL2 handler 可自由使用),x18-x28 由 EL2 保存恢复;未列 imm
 * 默认拒绝(fault 隔离死)。
 * IO 面(putc/getc)已退役,走 vuart MMIO 陷阱仿真(miniuart.h)。 */
#define HVC_CLOBBERS "x1","x2","x3","x4","x5","x6","x7","x8","x9", \
                     "x10","x11","x12","x13","x14","x15","x16","x17"

/* HVC #2 yield:主动让出 vCPU,VM 转 PAUSED;返回点 = 续跑断点 */
static inline void hvc_yield(void)
{
    register u64 x0 __asm__("x0") = 0;

    __asm__ volatile("hvc #2" : "+r"(x0) :: "memory", HVC_CLOBBERS);
    (void)x0;
}

/* HVC #4 host:前台 guest 主动交还 RX(宿主置 owner=NULL 并广播)。
 * 前台模式下宿主命令进不来——宿主让开 RX 就是这个语义的代价,
 * guest 交还后回轮询态(再 getc 时 -2 -> WFI 让位)。 */
static inline void hvc_host(void)
{
    register u64 x0 __asm__("x0") = 0;

    __asm__ volatile("hvc #4" : "+r"(x0) :: "memory", HVC_CLOBBERS);
    (void)x0;
}

static inline void hvc_exit(void)
{
    __asm__ volatile("hvc #1" ::: "memory");
    __builtin_unreachable();
}

/* HVC #5 fg:本客此刻是否前台 console owner(x0=1 是)。只读问询,
 * 无状态副作用——客据此自决是否印字(心跳门控:后台静默,免扰
 * 宿主与他客串口;vmconsole 0 前台化后自动恢复) */
static inline u64 hvc_fg(void)
{
    register u64 x0 __asm__("x0") = 0;

    __asm__ volatile("hvc #5" : "+r"(x0) :: "memory", HVC_CLOBBERS);
    return x0;
}

#endif /* GUEST_HVC_H */
