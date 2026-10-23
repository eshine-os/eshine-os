/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#ifndef ESHINE_VUART_H
#define ESHINE_VUART_H

#include <stdbool.h>

#include "kernel/include/types.h"

/* 27.04 第六站:per-VM 仿真串口(PL011 最小子集,DR/FR)。
 * guest 的 UART IPA 撤出 stage-2 映射,访问 data abort 陷阱化,
 * 由 kernel/mmio.c 按 IPA 窗口分发到本模型——参考实现 vuart 的最小真相版。
 * guest 只碰 DR/FR(QEMU 已把真 PL011 初始化好,guest 不碰 CR/波特率),
 * 子集与 guest 实际访问面严格对齐,其余寄存器读 0 写忽略。 */

#define VUART_RX_SIZE 64UL    /* host->guest 输入环(人类敲键 << 64B) */
#define VUART_TX_SIZE 128UL   /* 后台输出行缓冲([vmN] 前缀按整行打) */

struct vuart {
    char rx[VUART_RX_SIZE];
    u32  rx_h, rx_t;                 /* 生产者=宿主 shell,消费者=guest DR 读 */
    char tx[VUART_TX_SIZE];
    u32  tx_len;                     /* 行缓冲长度(0=空行缓冲) */
    bool tx_tagged;                  /* 行首定 tag:后台积累的行冲刷时带前缀 */
};

void vuart_init(struct vuart *vu);
void vuart_rx_push(struct vuart *vu, char c);      /* 宿主输入入环(满即丢) */
bool vuart_rx_has_data(const struct vuart *vu);

/* MMIO 语义(u32 访问,guest miniuart 为 volatile unsigned int *):
 * FR.RXFE=环空,FR.TXFF 恒 0(宿主 console 即时排空,guest TX 轮询不自旋);
 * DR 读弹环(空回 0,防御——单 CPU 无竞态,guest 按协议先查 FR)。 */
u64  vuart_mmio_read(struct vuart *vu, u64 off);
void vuart_mmio_write(struct vuart *vu, u32 vmid, u64 off, u64 val, bool fg);

void vuart_tx_flush(struct vuart *vu, u32 vmid);   /* owner 切换/退场:冲尾不吞半行 */

#endif /* ESHINE_VUART_H */
