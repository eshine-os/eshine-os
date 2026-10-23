/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#ifndef GUEST_BLK_H
#define GUEST_BLK_H

typedef unsigned long long u64;
typedef unsigned int u32;

/* vblk 驱动(第九站):内存即传输面,寄存器即门铃。数据缓冲是本
 * guest 自有 RAM 的一块静态区(它是唯一写者);MMIO 只碰门铃/状态。
 * 访存器全走 volatile asm 访问器(ISV=1 契约,第八站教训:
 * GCC 会合并出 post-index 回写型,宿主拿不到值)。 */
#define VBLK_BASE 0x0A000000UL

#define BLK_CMD    0x00u                 /* 写:[7:0]=op [23:8]=sector,写入即门铃 */
#define BLK_ADDR   0x04u                 /* 写:数据缓冲 IPA */
#define BLK_STATUS 0x08u                 /* 读:bit0 done bit1 err(读清) */
#define BLK_CAP    0x0Cu                 /* 读:扇区数 */
#define BLK_IMSC   0x10u                 /* 写:bit0 完成中断使能 */

#define BLK_OP_READ  1u
#define BLK_OP_WRITE 2u

static inline u32 blk_read(unsigned long off)
{
    unsigned long addr = VBLK_BASE + off;
    u32 v;

    __asm__ volatile("ldr %w0, [%1]" : "=r"(v) : "r"(addr));
    return v;
}

static inline void blk_write(unsigned long off, u32 v)
{
    unsigned long addr = VBLK_BASE + off;

    __asm__ volatile("str %w0, [%1]" :: "r"(v), "r"(addr) : "memory");
}

/* 数据面:512B 缓冲(guest 自有 .bss,宿主经静态映射直访) */
#define BLK_BUF_SECTOR 7u
static char blk_buf[512];

extern volatile unsigned int blk_done_count;   /* ISR 置位(完成事件计数) */

static inline void blk_irq_enable(void)
{
    blk_write(BLK_IMSC, 1u);
}

static inline void blk_isr(void)          /* IRQ 上下文:读 STATUS 清挂起 */
{
    (void)blk_read(BLK_STATUS);
    blk_done_count++;
}

/* 请求→WFI 等完成→返回(完成中断经 vSPI 34 泵注入唤醒) */
static inline void blk_do(u32 op)
{
    blk_write(BLK_ADDR, (u32)(unsigned long)blk_buf);
    blk_write(BLK_CMD, op | (BLK_BUF_SECTOR << 8));
    while (!blk_done_count)
        __asm__ volatile("wfi" ::: "memory");
    blk_done_count--;
}

#endif /* GUEST_BLK_H */
