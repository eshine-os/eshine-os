/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
/* vGIC distributor/redistributor 仿真(第十二站)。实现哲学:每寄存器
 * 按"真语义需要多真"分层——使能/探测/唤醒路径真语义(泵注入按它
 * 门控),纯状态寄存器存储回读,未知 RAZ/WI。写即完成(RWP 恒 0),
 * 轮询型 guest(Linux WAKER/CTLR.RWP 循环)零等待通过。
 * 宽度:guest 访存可 1/2/4/8 字节(SAS),子字访问按页内字节拼装;
 * ISENABLER/ICENABLER 保持手册的写置位/写清位语义(set-on-write 的
 * 是寄存器值本身,子字写按值置位——guest 全按字访问,退化形态不设)。 */

#include "drivers/include/vgic.h"

/* ---- 子字访问拼装(对齐访问,off&3 与 size 同族) ---- */

static u64 word_read(u32 v, u32 off, u32 size)
{
    return ((u64)v >> ((off & 3UL) * 8UL)) &
           (size == 1u ? 0xFFu : size == 2u ? 0xFFFFu : 0xFFFFFFFFu);
}

static void word_write(u32 *reg, u32 off, u32 size, u64 val)
{
    u32 shift = (off & 3UL) * 8UL;
    u32 mask = size == 1u ? 0xFFu : size == 2u ? 0xFFFFu : 0xFFFFFFFFu;

    *reg = (*reg & ~(mask << shift)) | (((u32)val & mask) << shift);
}

static u64 bytes_read(const u8 *base, u32 off, u32 size)
{
    u64 v = 0;
    u32 i;

    for (i = 0; i < size; i++)
        v |= (u64)base[off + i] << (i * 8UL);
    return v;
}

static void bytes_write(u8 *base, u32 off, u32 size, u64 val)
{
    u32 i;

    for (i = 0; i < size; i++)
        base[off + i] = (u8)(val >> (i * 8UL));
}

void vgic_reset(struct vgic *g)
{
    unsigned int i;

    g->dist_ctlr = 0;
    for (i = 0; i < VGIC_SPI_WORDS; i++) {
        g->dist_igroupr[i] = 0;
        g->dist_isenabler[i] = 0;
        g->dist_ipendr[i] = 0;
        g->dist_iactiver[i] = 0;
        g->dist_icfgr[i] = 0;
    }
    for (i = 0; i < sizeof(g->dist_ipriorityr); i++)
        g->dist_ipriorityr[i] = 0;
    g->rd_ctlr = 0;
    g->rd_waker = 0;
    g->sgi_igroupr0 = 0;
    g->sgi_isenabler0 = 0;
    g->sgi_ipendr0 = 0;
    g->sgi_iactiver0 = 0;
    for (i = 0; i < sizeof(g->sgi_ipriorityr); i++)
        g->sgi_ipriorityr[i] = 0;
    g->sgi_icfgr0 = 0;
    g->sgi_icfgr1 = 0;        /* PPI 全 level:与树里 PPI11 level 声明一致 */
}

/* ---- GICD(SPI 面:寄存器 n 覆盖 INTID 32(n+1) 起) ----
 * SPI 寄存器族分立解码:各族 0x80 宽(32 字寄存器),族基相差 0x80;
 * 族内字索引 n=(off-族基)>>2,只有 n < VGIC_SPI_WORDS 的字实存,
 * 其余 RAZ/WI。首版曾把整段 0x080-0x3FF 当一族共用 (off-0x080)>>2,
 * ISENABLER0(0x100) 被算成 n=32 越界——guest 使能回读验证当场抓获
 * (写静默 WI、读 RAZ,读回 0),教训:表驱动的族基必须显式分立。 */

static u32 dist_family_base(u32 off)
{
    static const u32 bases[7] = { 0x080u, 0x100u, 0x180u, 0x200u,
                                  0x280u, 0x300u, 0x380u };
    u32 i;

    for (i = 0; i < 7; i++)
        if (off >= bases[i] && off < bases[i] + 0x80u)
            return bases[i];
    return 0xFFFFFFFFu;
}

u64 vgic_dist_mmio_read(struct vgic *g, u32 off, u32 size)
{
    u32 base, n;

    if (off == 0xFFE8u)                 /* PIDR2:GICv3 判据位 [7:4]=3 */
        return 0x30u;
    if (off == 0x004u)                  /* TYPER:ITLinesNumber=1(INTID 32-63) */
        return word_read(0x1u, off, size);
    if (off == 0x008u)                  /* IIDR:ES implementer,外观而已 */
        return word_read(0x45533000u, off, size);
    if (off == 0x000u)                  /* CTLR:Enable 位(RWP 恒 0,写即完成) */
        return word_read(g->dist_ctlr, off, size);
    if (off >= 0x400u && off + size <= 0x400u + sizeof(g->dist_ipriorityr))
        return bytes_read(g->dist_ipriorityr, off - 0x400u, size);
    if (off >= 0xC00u && (off & 3UL) == 0) {
        n = (off - 0xC00u) >> 2;
        if (n < VGIC_SPI_WORDS)
            return word_read(g->dist_icfgr[n], off, size);
        return 0;
    }
    base = dist_family_base(off);
    if (base == 0xFFFFFFFFu || (off & 3UL) != 0)
        return 0;                        /* 未知:RAZ */
    /* 站十四规范对齐:家族内 word0 = INTID 0-31 银行视图(GICv3 属当前
     * CPU 重分发器面,guest 不应从 GICD 动——RAZ 诚实拒绝);word1 起
     * = SPI 32-63(Linux 按 IHI0069 写 ISENABLER1=0x104,旧译码把这
     * 发丢在 n>=VGIC_SPI_WORDS 里,guest 的 SPI 从未使能,gate 实测)。 */
    n = (off - base) >> 2;
    if (n == 0u)
        return 0;                        /* 银行 word:RAZ */
    n -= 1u;
    if (n >= VGIC_SPI_WORDS)
        return 0;
    switch (base) {
    case 0x080u:                         /* IGROUPR */
        return word_read(g->dist_igroupr[n], off, size);
    case 0x100u:                         /* ISENABLER */
    case 0x180u:                         /* ICENABLER 读同使能态 */
        return word_read(g->dist_isenabler[n], off, size);
    case 0x200u:                         /* ISPENDR */
    case 0x280u:                         /* ICPENDR 读同挂起态 */
        return word_read(g->dist_ipendr[n], off, size);
    default:                             /* IS/ICACTIVER 读同活跃态 */
        return word_read(g->dist_iactiver[n], off, size);
    }
}

void vgic_dist_mmio_write(struct vgic *g, u32 off, u64 val, u32 size)
{
    u32 base, n;

    if (off == 0x000u) {                 /* CTLR:存 Enable 位,RWP 恒 0 */
        word_write(&g->dist_ctlr, off, size, val & 1);
        return;
    }
    if (off >= 0x400u && off + size <= 0x400u + sizeof(g->dist_ipriorityr)) {
        bytes_write(g->dist_ipriorityr, off - 0x400u, size, val);
        return;
    }
    if (off >= 0xC00u && (off & 3UL) == 0) {
        n = (off - 0xC00u) >> 2;
        if (n < VGIC_SPI_WORDS)
            word_write(&g->dist_icfgr[n], off, size, val);
        return;
    }
    base = dist_family_base(off);
    if (base == 0xFFFFFFFFu || (off & 3UL) != 0)
        return;                           /* 未知:WI */
    /* 同读侧:word0 = 银行视图 WI(SGI/PPI 请走 GICR SGI 帧),
     * word1 起 = SPI 面(Linux 写 0x104/0x184/... 落此)。 */
    n = (off - base) >> 2;
    if (n == 0u)
        return;
    n -= 1u;
    if (n >= VGIC_SPI_WORDS)
        return;
    if (base == 0x080u) {                /* IGROUPR:直写 */
        word_write(&g->dist_igroupr[n], off, size, val);
        return;
    }
    if (base == 0x100u) {                /* ISENABLER:写置位 */
        g->dist_isenabler[n] |= (u32)val;
        return;
    }
    if (base == 0x180u) {                /* ICENABLER:写清位 */
        g->dist_isenabler[n] &= ~(u32)val;
        return;
    }
    if (base == 0x200u) {                /* ISPENDR:写置位 */
        g->dist_ipendr[n] |= (u32)val;
        return;
    }
    if (base == 0x280u) {                /* ICPENDR:写清位 */
        g->dist_ipendr[n] &= ~(u32)val;
        return;
    }
    if (base == 0x300u) {                /* ISACTIVER:写置位 */
        g->dist_iactiver[n] |= (u32)val;
        return;
    }
    /* 0x380u ICACTIVER:写清位 */
    g->dist_iactiver[n] &= ~(u32)val;
}

/* ---- GICR(RD 帧 0x0000-0xFFFF + SGI 帧 0x10000-0x1FFFF) ---- */

#define GICR_SGI 0x10000u

u64 vgic_redist_mmio_read(struct vgic *g, u32 off, u32 size)
{
    if (off == 0xFFE8u)                  /* PIDR2:GICv3 判据位 [7:4]=3——RD 帧
                                          * 身份证;Linux gic_iterate_rdists
                                          * 先验它再谈 TYPER(本站战况:漏配
                                          * 它 ="No redistributor present") */
        return 0x30u;
    if (off == 0x008u)                   /* TYPER:亲和全 0,ProcessorNumber=0,
                                          * Last(bit4)=1:单帧单核 */
        return word_read(0x10u, off, size);
    if (off == 0x004u)                   /* IIDR */
        return word_read(0x45533000u, off, size);
    if (off == 0x000u)                   /* CTLR:Enable 位(RWP 恒 0) */
        return word_read(g->rd_ctlr, off, size);
    if (off == 0x014u)                   /* WAKER:存储回读(清睡眠即零,
                                          * ChildrenAsleep 跟随写值) */
        return word_read(g->rd_waker, off, size);
    if (off >= GICR_SGI + 0x400u &&
        off + size <= GICR_SGI + 0x400u + sizeof(g->sgi_ipriorityr))
        return bytes_read(g->sgi_ipriorityr, off - (GICR_SGI + 0x400u), size);
    if (off >= GICR_SGI && (off & 3UL) == 0) {
        u32 so = off - GICR_SGI;         /* SGI 帧:INTID 0-31 */

        if (so == 0x080u)
            return word_read(g->sgi_igroupr0, so, size);
        if (so == 0x100u || so == 0x180u)   /* I/ICENABLER0 读同使能态 */
            return word_read(g->sgi_isenabler0, so, size);
        if (so == 0x200u || so == 0x280u)   /* I/ICPENDR0 读同挂起态 */
            return word_read(g->sgi_ipendr0, so, size);
        if (so == 0x300u || so == 0x380u)   /* I/ICACTIVER0 读同活跃态 */
            return word_read(g->sgi_iactiver0, so, size);
        if (so == 0xC00u)
            return word_read(g->sgi_icfgr0, so, size);
        if (so == 0xC04u)
            return word_read(g->sgi_icfgr1, so, size);
    }
    return 0;                            /* 未知:RAZ */
}

void vgic_redist_mmio_write(struct vgic *g, u32 off, u64 val, u32 size)
{
    u32 v = (u32)val;

    if (off == 0x000u) {
        word_write(&g->rd_ctlr, off, size, val & 1);
        return;
    }
    if (off == 0x014u) {
        word_write(&g->rd_waker, off, size, val);
        return;
    }
    if (off >= GICR_SGI + 0x400u &&
        off + size <= GICR_SGI + 0x400u + sizeof(g->sgi_ipriorityr)) {
        bytes_write(g->sgi_ipriorityr, off - (GICR_SGI + 0x400u), size, val);
        return;
    }
    if (off >= GICR_SGI && (off & 3UL) == 0) {
        u32 so = off - GICR_SGI;

        if (so == 0x080u)                /* IGROUPR0:直写 */
            word_write(&g->sgi_igroupr0, so, size, val);
        else if (so == 0x100u)           /* ISENABLER0:写置位(PPI 27=bit27) */
            g->sgi_isenabler0 |= v;
        else if (so == 0x180u)           /* ICENABLER0:写清位 */
            g->sgi_isenabler0 &= ~v;
        else if (so == 0x200u)
            g->sgi_ipendr0 |= v;
        else if (so == 0x280u)
            g->sgi_ipendr0 &= ~v;
        else if (so == 0x300u)
            g->sgi_iactiver0 |= v;
        else if (so == 0x380u)
            g->sgi_iactiver0 &= ~v;
        else if (so == 0xC00u)
            word_write(&g->sgi_icfgr0, so, size, val);
        else if (so == 0xC04u)
            word_write(&g->sgi_icfgr1, so, size, val);
        return;
    }
    /* 未知/只读(TYPER/IIDR):WI */
}

/* ---- 泵注入门控:distributor 是权威 ---- */

bool vgic_spi_enabled(const struct vgic *g, u32 intid)
{
    u32 idx = intid - 32u;

    if (intid < 32u || intid >= 32u + 32u * VGIC_SPI_WORDS)
        return false;
    return (g->dist_isenabler[idx >> 5] >> (idx & 0x1Fu)) & 1u;
}

bool vgic_ppi_enabled(const struct vgic *g, u32 intid)
{
    if (intid >= 32u)
        return false;
    return (g->sgi_isenabler0 >> intid) & 1u;
}

/* 站十四:guest 自配的 PPI 优先级(IPRIORITYR SGI 帧镜像)。
 * 注入 LR 的优先级随它——硬编码 0x00 会落进 Linux pseudo-NMI
 * 阈值内,把 tick 走成 NMI 路径;guest 配了什么就用什么。 */
u8 vgic_ppi_prio(const struct vgic *g, u32 intid)
{
    if (intid >= 32u)
        return 0u;
    return g->sgi_ipriorityr[intid];
}
