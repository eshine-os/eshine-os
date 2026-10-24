/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#ifndef ESHINE_VUART_H
#define ESHINE_VUART_H

#include <stdbool.h>

#include "kernel/include/types.h"

/* 27.04 第六站:per-VM 仿真串口(PL011)→ 第十三站长成 Linux 驱动
 * 可绑形态:AMBA 身份证(PERIPHID/PCELLID 只读)+ 控制寄存器族
 * (CR/IBRD/FBRD/LCR_H/IFLS/DMACR 存储回读)+ CR 真语义
 * (DR 写要 UARTEN|TXE,RX 中断要 UARTEN|RXE)。
 * guest 的 UART IPA 撤出 stage-2 映射,访问 data abort 陷阱化,
 * 由 kernel/mmio.c 按 IPA 窗口分发到本模型——参考实现 vuart 的真相版。
 * 波特率不仿真(无时钟即无时序);错误位/流控/DMA 无消费者不建。 */

#define VUART_RX_SIZE 64UL    /* host->guest 输入环(人类敲键 << 64B) */
#define VUART_TX_SIZE 128UL   /* 后台输出行缓冲([vmN] 前缀按整行打) */

struct vuart {
    char rx[VUART_RX_SIZE];
    u32  rx_h, rx_t;                 /* 生产者=宿主 shell,消费者=guest DR 读 */
    char tx[VUART_TX_SIZE];
    u32  tx_len;                     /* 行缓冲长度(0=空行缓冲) */
    bool tx_tagged;                  /* 行首定 tag:后台积累的行冲刷时带前缀 */
    u32  imsc;                       /* 中断屏蔽:仅 RXIM(bit4)建模 */
    u32  ris;                        /* 原始挂起:RX(bit4),读 DR 自动清 */
    u32  cr, ibrd, fbrd, lcr_h, ifls, dmacr;   /* 第十三站:控制族存储回读 */
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

/* RX 中断就绪 = RXIM 使能且有挂起:宿主据此置泵注入标志(第八站) */
bool vuart_rx_irq_ready(const struct vuart *vu);

#endif /* ESHINE_VUART_H */
