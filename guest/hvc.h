/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#ifndef GUEST_HVC_H
#define GUEST_HVC_H

typedef unsigned long long u64;

/* hypercall ABI(ARCHITECTURE.md): x0 入参/返回,x1-x17 全部易失
 * (EL2 handler 可自由使用),x18-x28 由 EL2 保存恢复 */
#define HVC_CLOBBERS "x1","x2","x3","x4","x5","x6","x7","x8","x9", \
                     "x10","x11","x12","x13","x14","x15","x16","x17"

static inline void hvc_putc(char c)
{
    register u64 x0 __asm__("x0") = (u64)(unsigned char)c;

    __asm__ volatile("hvc #0" : "+r"(x0) :: "memory", HVC_CLOBBERS);
    (void)x0;
}

/* HVC #2 yield:主动让出 vCPU,VM 转 PAUSED;返回点 = 续跑断点 */
static inline void hvc_yield(void)
{
    register u64 x0 __asm__("x0") = 0;

    __asm__ volatile("hvc #2" : "+r"(x0) :: "memory", HVC_CLOBBERS);
    (void)x0;
}

static inline void hvc_exit(void)
{
    __asm__ volatile("hvc #1" ::: "memory");
    __builtin_unreachable();
}

#endif /* GUEST_HVC_H */
