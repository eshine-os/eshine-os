/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#ifndef GUEST_MINIUART_H
#define GUEST_MINIUART_H

/* QEMU virt PL011。guest 走纯 stage-2(SCTLR_EL1.M=0),VA 即 IPA。
 * 第六站:UART IPA 已撤出 stage-2 映射(仿真窗口)——DR/FR 访问全部
 * data abort 陷阱化,宿主 per-VM vuart 按寄存器语义仿真。驱动不改
 * 语义:TX 轮询代码零改动;RX 从 HVC 三态换回 PL011 原生 FR 轮询。 */
#define UART0_BASE 0x09000000UL

#define UART_DR   0x00u
#define UART_FR   0x18u                 /* PL011 FR=0x18;0x14 未实现,读恒 0
                                          * → 曾致 RX 无门控热自旋,与 QEMU
                                          *   chardev 推流赛跑,偶发字节重复/丢失 */
#define FR_RXFE   (1u << 4)
#define FR_TXFF   (1u << 5)

/* 访存器一律走 volatile asm 访问器:形式上保证每条访存都是可综合症的
 * plain ldr/str [xN](ISV=1)——第四站起实测 GCC 会自作聪明合并出
 * post-index 回写型(str w?,[x?],#-32,ISV=0 无寄存器现场),宿主仿真
 * 拿不到值只能拒;asm 访问器从根上封死这类代码生成。 */
static inline unsigned int uart_read(unsigned long off)
{
    unsigned long addr = UART0_BASE + off;
    unsigned int v;

    __asm__ volatile("ldr %w0, [%1]" : "=r"(v) : "r"(addr));
    return v;
}

static inline void uart_write(unsigned long off, unsigned int v)
{
    unsigned long addr = UART0_BASE + off;

    __asm__ volatile("str %w0, [%1]" :: "r"(v), "r"(addr) : "memory");
}

#define UART_IMSC 0x38u                 /* 第八站:RXIM 使能 -> 宿主虚注 vSPI 33 */
#define IRQ_RX    (1u << 4)
#define UART_IBRD 0x24u                 /* 第十三站:控制族(Linux pl011 同律) */
#define UART_FBRD 0x28u
#define UART_LCRH 0x2Cu
#define UART_CR   0x30u
#define UART_IFLS 0x34u
#define UART_DMACR 0x48u
#define UART_PIDR0 0xFE0u               /* AMBA 身份证:PERIPHID/PCELLID */
#define UART_CIDR0  0xFF0u
#define CR_UARTEN 0x001u
#define CR_TXE    0x100u
#define CR_RXE    0x200u

static inline void uart_putc(char c)
{
    while (uart_read(UART_FR) & FR_TXFF)
        ;
    uart_write(UART_DR, (unsigned char)c);
}

/* 串输出原语(第十一站从 main.c 收编进驱动头:main/fdt 共用) */
static inline void puts_uart(const char *s)
{
    while (*s)
        uart_putc(*s++);
}

/* 第八站:RX 改中断驱动。宿主 vuart 入环置 RIS,RXIM 使能即虚注
 * vSPI 33(LR1);handler 读 DR 汇入 guest 软环(读 DR 自动清 RIS),
 * getc 从软环取,空则 WFI——IRQ 填环后同一 WFI 醒来续取。 */
#define GRX_SIZE 32u
static char grx[GRX_SIZE];
static volatile unsigned int grx_h, grx_t;

/* 第十三站:Linux pl011 驱动同律 bring-up。
 * AMBA 身份证自查:PERIPHID 八字节组 AMBA id 0x00141011、PCELLID
 * 组 0xB105F00D——设备证明自己是谁(总线靠它绑驱动);过则自报,
 * 败则报 FAIL(门禁必抓)。后接关机->配置->开机三段式:
 * CR=0(关)→ IBRD=13/FBRD=1(115200@24MHz,无时序只登记)→
 * LCR_H=8N1+FEN → IFLS/DMACR → IMSC=RXIM → CR=0x301(UARTEN|TXE|RXE)。 */
static inline void uart_pl011_init(void)
{
    unsigned int id, amba_ok, cell_ok;

    id = (uart_read(UART_PIDR0 + 0x0Cu) << 24) | (uart_read(UART_PIDR0 + 0x08u) << 16) |
         (uart_read(UART_PIDR0 + 0x04u) << 8) | uart_read(UART_PIDR0);
    amba_ok = (id == 0x00141011u);
    id = (uart_read(UART_CIDR0 + 0x0Cu) << 24) | (uart_read(UART_CIDR0 + 0x08u) << 16) |
         (uart_read(UART_CIDR0 + 0x04u) << 8) | uart_read(UART_CIDR0);
    cell_ok = (id == 0xB105F00Du);

    uart_write(UART_CR, 0u);            /* 关机再配置(Linux 同律) */
    uart_write(UART_IBRD, 13u);         /* 115200 @ 24MHz */
    uart_write(UART_FBRD, 1u);
    uart_write(UART_LCRH, 0x70u);       /* 8N1 + FEN */
    uart_write(UART_IFLS, 0x12u);
    uart_write(UART_DMACR, 0u);
    uart_write(UART_IMSC, IRQ_RX);
    uart_write(UART_CR, CR_UARTEN | CR_TXE | CR_RXE);

    /* 自报在开机后:真 PL011 同律,开机前 DR 写被 CR 门丢弃(本站
     * 战况——gic 自报曾随开机前输出一起被吞)。FAIL 也要有通道。 */
    if (amba_ok && cell_ok)
        puts_uart("pl011 on: amba 00141011\n");
    else
        puts_uart("pl011 FAIL: id\n");
}

static inline void uart_rx_isr(void)      /* IRQ 上下文:排空整批入软环 */
{
    while (!(uart_read(UART_FR) & FR_RXFE)) {   /* 真 UART 驱动同律:一发清空 */
        unsigned int n = (grx_h + 1) % GRX_SIZE;
        char c = (char)uart_read(UART_DR);

        if (n != grx_t) {
            grx[grx_h] = c;
            grx_h = n;
        }                                /* 软环满丢新(读 DR 已清挂起) */
    }
}

static inline char uart_getc(void)
{
    for (;;) {
        unsigned int t = grx_t;

        if (t != grx_h) {
            grx_t = (t + 1) % GRX_SIZE;
            return grx[t];
        }
        __asm__ volatile("wfi");
    }
}

#endif /* GUEST_MINIUART_H */
