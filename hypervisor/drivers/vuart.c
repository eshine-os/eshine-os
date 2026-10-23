/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
/* vuart:per-VM 仿真串口(DR/FR 子集 + RX 环 + TX 行缓冲前缀) */
#include "drivers/include/vuart.h"
#include "kernel/include/console.h"
#include "kernel/include/util.h"

#define UART_DR   0x00UL
#define UART_FR   0x18UL
#define UART_IMSC 0x38UL               /* 第八站中断面:仅 RXIM 建模 */
#define UART_RIS  0x3CUL
#define UART_MIS  0x40UL
#define UART_ICR  0x44UL
#define FR_RXFE   (1u << 4)
#define FR_TXFF   (1u << 5)            /* 恒 0:见 vuart_mmio_read */
#define IRQ_RX    (1u << 4)            /* RXIM/RIS 的 RX 位 */

void vuart_init(struct vuart *vu)
{
    vu->rx_h = 0;
    vu->rx_t = 0;
    vu->tx_len = 0;
    vu->tx_tagged = false;
    vu->imsc = 0;
    vu->ris = 0;
}

void vuart_rx_push(struct vuart *vu, char c)
{
    u32 n = (vu->rx_h + 1) % VUART_RX_SIZE;

    if (n == vu->rx_t)
        return;                        /* 环满即丢:64B 对人类敲键是天文数字 */
    vu->rx[vu->rx_h] = c;
    vu->rx_h = n;
    vu->ris |= IRQ_RX;                 /* 有数据到:原始挂起(真 PL011 同律) */
}

bool vuart_rx_has_data(const struct vuart *vu)
{
    return vu->rx_h != vu->rx_t;
}

static u32 vuart_rx_pop(struct vuart *vu)
{
    u32 c = (u32)(u8)vu->rx[vu->rx_t];

    vu->rx_t = (vu->rx_t + 1) % VUART_RX_SIZE;
    return c;
}

/* 冲刷行缓冲:tagged 行打 "[vmN] " 前缀;半行(owner 切换/VM 死亡)照冲 */
static void vuart_tx_emit(struct vuart *vu, u32 vmid)
{
    u32 i;

    if (vu->tx_len == 0)
        return;
    if (vu->tx_tagged) {
        console_puts("[vm");
        util_putdec(vmid);
        console_puts("] ");
    }
    for (i = 0; i < vu->tx_len; i++)
        console_putc(vu->tx[i]);
    vu->tx_len = 0;
}

u64 vuart_mmio_read(struct vuart *vu, u64 off)
{
    switch (off) {
    case UART_DR: {
        u64 v = vuart_rx_has_data(vu) ? vuart_rx_pop(vu) : 0UL;

        /* 读 DR 重评 RX 挂起(真 PL011 同律:环内仍有数据则保持)——
         * 多字符爆发:每注一发弹一个,泵入口见 irq_ready 续注直至排空 */
        vu->ris = vuart_rx_has_data(vu) ? IRQ_RX : 0u;
        return v;
    }
    case UART_IMSC:
        return vu->imsc;
    case UART_RIS:
        return vu->ris;
    case UART_MIS:
        return vu->ris & vu->imsc;
    case UART_FR:
        /* RXFE=环空;TXFF=0(宿主即时排空);BUSY=0 */
        return vuart_rx_has_data(vu) ? 0UL : FR_RXFE;
    default:
        return 0UL;                    /* CR/IBRD/ICR...:读 0 写忽略,不 fault */
    }
}

void vuart_mmio_write(struct vuart *vu, u32 vmid, u64 off, u64 val, bool fg)
{
    char c = (char)val;

    if (off == UART_IMSC) {
        vu->imsc = (u32)val & IRQ_RX;  /* 仅 RXIM 建模:其余位写入忽略 */
        return;
    }
    if (off == UART_ICR) {
        vu->ris &= ~((u32)val & IRQ_RX);
        return;
    }
    if (off != UART_DR)
        return;                        /* FR/CR 等写忽略 */
    if (fg) {
        /* 前台裸透传;切前台前若残留后台半行,按其 tag 先冲 */
        vuart_tx_emit(vu, vmid);
        console_putc(c);
        return;
    }
    if (vu->tx_len == 0)
        vu->tx_tagged = true;          /* 行首定 tag:整行按后台格式冲 */
    vu->tx[vu->tx_len++] = c;
    if (c == '\n' || vu->tx_len == VUART_TX_SIZE)
        vuart_tx_emit(vu, vmid);
}

void vuart_tx_flush(struct vuart *vu, u32 vmid)
{
    vuart_tx_emit(vu, vmid);
}

bool vuart_rx_irq_ready(const struct vuart *vu)
{
    return (vu->imsc & vu->ris & IRQ_RX) != 0u;
}
