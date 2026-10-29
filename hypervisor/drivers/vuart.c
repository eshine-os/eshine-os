/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
/* vuart:per-VM 仿真串口(DR/FR 子集 + RX 环 + TX 行缓冲前缀) */
#include "drivers/include/vuart.h"
#include "kernel/include/console.h"
#include "kernel/include/util.h"

#define UART_DR   0x00UL
#define UART_FR   0x18UL
#define UART_IBRD 0x24UL               /* 第十三站:控制族,存储回读 */
#define UART_FBRD 0x28UL
#define UART_LCRH 0x2CUL
#define UART_CR   0x30UL
#define UART_IFLS 0x34UL
#define UART_IMSC 0x38UL               /* 第八站中断面:仅 RXIM 建模 */
#define UART_RIS  0x3CUL
#define UART_MIS  0x40UL
#define UART_ICR  0x44UL
#define UART_DMACR 0x48UL
#define FR_RXFE   (1u << 4)
#define FR_TXFF   (1u << 5)            /* 恒 0:见 vuart_mmio_read */
#define IRQ_RX    (1u << 4)            /* RXIM/RIS 的 RX 位 */
#define CR_UARTEN 0x001u               /* CR 真语义:开机位 */
#define CR_TXE    0x100u               /* 发送使能 */
#define CR_RXE    0x200u               /* 接收使能 */

void vuart_init(struct vuart *vu)
{
    vu->rx_h = 0;
    vu->rx_t = 0;
    vu->tx_len = 0;
    vu->tx_tagged = false;
    vu->imsc = 0;
    vu->ris = 0;
    vu->cr = 0x300u;                    /* 真复位值:TXE|RXE,UARTEN=0 */
    vu->ibrd = 0;
    vu->fbrd = 0;
    vu->lcr_h = 0;
    vu->ifls = 0x12u;                   /* 真复位值:默认 FIFO 水位 */
    vu->dmacr = 0;
}

/* 站20:三物理核下 vuart 状态面出现真并发写者——CPU0 宿主 shell
 * (rx_push/冲刷)与 CPU1/CPU2 专泵上的 guest 陷阱(DR 读弹环重评
 * ris / bg TX 追加)。裸奔实测两处血案:push 的 ris|= 与 DR 读的
 * ris=0 整写赛跑,字节在环而挂起清零,注入永不再来=真死客(e1
 * "无回声"形的机制面);bg 追加与 vmconsole 冲刷对 tx_len 无序交
 * 错,半行被复制/字节丢失([62] 提示符错形)。console 锁(站18D,
 * 同 CPU 递归、跨 CPU 互斥)串行化;单核拓扑两写者同核本无并发,
 * 行为零变。 */
void vuart_rx_push(struct vuart *vu, char c)
{
    u32 n;

    console_lock();
    n = (vu->rx_h + 1) % VUART_RX_SIZE;
    if (n != vu->rx_t) {
        vu->rx[vu->rx_h] = c;
        vu->rx_h = n;
        vu->ris |= IRQ_RX;             /* 有数据到:原始挂起(真 PL011 同律) */
    }                                  /* 环满即丢:64B 对人类敲键是天文数字 */
    console_unlock();
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
    console_lock();                  /* 站十八D:整段冲刷行级原子 */
    if (vu->tx_tagged) {
        console_puts("[vm");
        util_putdec(vmid);
        console_puts("] ");
    }
    for (i = 0; i < vu->tx_len; i++)
        console_putc(vu->tx[i]);
    vu->tx_len = 0;
    console_unlock();
}

u64 vuart_mmio_read(struct vuart *vu, u64 off)
{
    switch (off) {
    case UART_DR: {
        u64 v;

        console_lock();                /* 站20:弹环+ris 重评与 push 互斥 */
        v = vuart_rx_has_data(vu) ? vuart_rx_pop(vu) : 0UL;
        /* 读 DR 重评 RX 挂起(真 PL011 同律:环内仍有数据则保持)——
         * 多字符爆发:每注一发弹一个,泵入口见 irq_ready 续注直至排空 */
        vu->ris = vuart_rx_has_data(vu) ? IRQ_RX : 0u;
        console_unlock();
        return v;
    }
    case UART_IMSC:
        return vu->imsc;
    case UART_RIS:
        return vu->ris;
    case UART_MIS:
        return vu->ris & vu->imsc;
    case UART_CR:
        return vu->cr;
    case UART_IBRD:
        return vu->ibrd;
    case UART_FBRD:
        return vu->fbrd;
    case UART_LCRH:
        return vu->lcr_h;
    case UART_IFLS:
        return vu->ifls;
    case UART_DMACR:
        return vu->dmacr;
    case UART_FR:
        /* RXFE=环空;TXFF=0(宿主即时排空);BUSY=0 */
        return vuart_rx_has_data(vu) ? 0UL : FR_RXFE;
    case 0xFE0: return 0x11UL;         /* PERIPHID:PL011 id 0x00141011 */
    case 0xFE4: return 0x10UL;
    case 0xFE8: return 0x14UL;
    case 0xFEC: return 0x00UL;
    case 0xFF0: return 0x0DUL;         /* PCELLID:0xB105F00D */
    case 0xFF4: return 0xF0UL;
    case 0xFF8: return 0x05UL;
    case 0xFFC: return 0xB1UL;
    default:
        return 0UL;                    /* RSR/ECR/ILPR 等:读 0 写忽略,不 fault */
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
    if (off == UART_CR) {               /* 控制族:存储回读(波特率无时序) */
        vu->cr = (u32)val;
        return;
    }
    if (off == UART_IBRD) {
        vu->ibrd = (u32)val;
        return;
    }
    if (off == UART_FBRD) {
        vu->fbrd = (u32)val;
        return;
    }
    if (off == UART_LCRH) {
        vu->lcr_h = (u32)val;
        return;
    }
    if (off == UART_IFLS) {
        vu->ifls = (u32)val;
        return;
    }
    if (off == UART_DMACR) {
        vu->dmacr = (u32)val;
        return;
    }
    if (off != UART_DR)
        return;                        /* FR/身份证等写忽略 */
    /* CR 语义:TX 门 = TXE(第十四站放宽:内核 pl011 earlycon 只挂
     * write 回调直写 DR,不置 UARTEN——Linux 6.6 源码实证;真芯片此处
     * TRM 有模糊区,QEMU 亦不关门。仿真器务实主义:门在配置面(TXE),
     * 不在开关面(UARTEN)。RX 中断门保留 UARTEN|RXE 双条件不动。 */
    if ((vu->cr & CR_TXE) == 0u)
        return;
    if (fg) {
        /* 站22A([54] 三案定谳):fg 由逐字符裸透传改行缓冲——裸透传
         * 的字符间缝隙留给跨核 bg 行整行嵌入(tick 抢切落点窗),
         * needle 撕裂是概率性的;入缓冲后 '\n' 经 vuart_tx_emit 锁内
         * 整行原子,判据 needle 确定性。交互 UX(键回显即时)由
         * 泊位冲刷保:客 WFI 停泊即冲半行(exceptions.c TWI 分支),
         * 人类敲键 100ms 尺度无感。残留后台半行先冲(tag 归它),
         * fg 行 tx_tagged=false 与 bg(true)对称。 */
        console_lock();
        if (vu->tx_len)
            vuart_tx_emit(vu, vmid);   /* 复入只增嵌套 */
        vu->tx_tagged = false;
        vu->tx[vu->tx_len++] = c;
        console_unlock();
        if (c == '\n' || vu->tx_len == VUART_TX_SIZE)
            vuart_tx_emit(vu, vmid);
        return;
    }
    console_lock();                   /* 站20:bg 追加与冲刷/vuart_tx_emit
                                        * 共锁串行(同 CPU 递归,emit 复入
                                        * 只增嵌套);跨 CPU 半行不再
                                        * 被对面的冲刷撕开 */
    if (vu->tx_len == 0)
        vu->tx_tagged = true;          /* 行首定 tag:整行按后台格式冲 */
    vu->tx[vu->tx_len++] = c;
    if (c == '\n' || vu->tx_len == VUART_TX_SIZE) {
        console_unlock();
        vuart_tx_emit(vu, vmid);
        return;
    }
    console_unlock();
}

void vuart_tx_flush(struct vuart *vu, u32 vmid)
{
    vuart_tx_emit(vu, vmid);
}

bool vuart_rx_irq_ready(const struct vuart *vu)
{
    return (vu->cr & (CR_UARTEN | CR_RXE)) == (CR_UARTEN | CR_RXE) &&
           (vu->imsc & vu->ris & IRQ_RX) != 0u;
}
