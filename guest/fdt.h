/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#ifndef ESHINE_GUEST_FDT_H
#define ESHINE_GUEST_FDT_H

/* 第十一站:mini-FDT 走查器(只读)。宿主 vm_create 把设备树整拷到
 * 固定偏移(IPA 0x40F00000,bootloader 同构);guest 引导期自查三节点,
 * 设备身份从树查出而非硬编码。能力面严格对齐判据:头校验 + 路径
 * 深度匹配 + compatible 首串读取;FDT 串恒 NUL 结,直接交 puts_uart。
 * 深色细节:结构块大端 u32 令牌流,全部按字节游标走(无单位换算坑),
 * 游标以 size_dt_struct 兜底(坏 blob 不许拖 guest 空转)。 */

#include "miniuart.h"

#define FDT_ADDR       0x40F00000UL          /* 与宿主 VM_DTB_OFF(0xF00000) 对表 */
#define FDT_MAGIC      0xd00dfeedu
#define FDT_TOK_BEGIN  0x1u
#define FDT_TOK_END_N  0x2u
#define FDT_TOK_PROP   0x3u
#define FDT_TOK_NOP    0x4u
#define FDT_TOK_END    0x9u

static const unsigned char *fdt_blob;   /* blob 基址 */
static const unsigned char *fdt_dt;     /* 结构块 */
static const unsigned char *fdt_str;    /* 串块 */
static const unsigned char *fdt_dt_end; /* 结构块尾(游标兜底) */

static unsigned int fdt_be32(const unsigned char *p)
{
    return ((unsigned int)p[0] << 24) | ((unsigned int)p[1] << 16) |
           ((unsigned int)p[2] << 8) | (unsigned int)p[3];
}

static unsigned int fdt_strlen(const char *s)
{
    unsigned int n = 0;
    while (s[n]) n++;
    return n;
}

static int fdt_streq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

/* 节点名与路径组件全串比(组件名后必须恰为 NUL) */
static int fdt_name_eq(const char *nm, const char *comp, unsigned int clen)
{
    unsigned int i;
    for (i = 0; i < clen; i++)
        if (nm[i] != comp[i])
            return 0;
    return nm[clen] == '\0';
}

static int fdt_init(const void *blob)
{
    const unsigned char *b = (const unsigned char *)blob;
    if (fdt_be32(b) != FDT_MAGIC)
        return 0;
    if (fdt_be32(b + 20) < 16u)          /* version:结构块自 v16 稳定 */
        return 0;
    fdt_blob   = b;
    fdt_dt     = b + fdt_be32(b + 8);    /* off_dt_struct */
    fdt_str    = b + fdt_be32(b + 12);   /* off_dt_strings */
    fdt_dt_end = fdt_dt + fdt_be32(b + 36); /* size_dt_struct */
    return 1;
}

/* 深度优先找节点:路径 "/a/b" 按层匹配(matched 计已配层级,根预配 1)。
 * 命中返回节点内首令牌游标;PROP 令牌在行进中跳过。 */
static const unsigned char *fdt_find(const char *path)
{
    const char *comp[8];
    unsigned int clen[8], ncomp = 0;
    const char *s;
    const unsigned char *p = fdt_dt;
    unsigned int matched = 1, open = 0;

    for (s = path + 1; *s; ) {
        comp[ncomp] = s;
        clen[ncomp] = 0;
        while (*s && *s != '/') { s++; clen[ncomp]++; }
        if (*s == '/') s++;
        if (++ncomp == 8)
            return 0;
    }
    while (p < fdt_dt_end) {
        unsigned int tok = fdt_be32(p);
        p += 4;
        if (tok == FDT_TOK_END)
            return 0;
        if (tok == FDT_TOK_NOP)
            continue;
        if (tok == FDT_TOK_BEGIN) {
            const char *nm = (const char *)p;
            p += (fdt_strlen(nm) + 4) & ~3u;      /* 名+NUL 后 4 对齐 */
            if (matched == open && open >= 1 && open <= ncomp &&
                fdt_name_eq(nm, comp[open - 1], clen[open - 1])) {
                matched++;
                if (matched == ncomp + 1)
                    return p;
            }
            open++;
        } else if (tok == FDT_TOK_END_N) {
            if (matched == open && matched > 1)
                matched--;
            if (open > 0)
                open--;
        } else if (tok == FDT_TOK_PROP) {
            unsigned int len = fdt_be32(p);       /* len + nameoff 后数据 4 对齐 */
            p += 8 + ((len + 3) & ~3u);
        } else {
            return 0;                              /* 未知令牌:拒绝 */
        }
    }
    return 0;
}

/* 节点内找字符串属性:连续 PROP 段扫到首个非 PROP(子节点/出节点)止。
 * 命中返回数据首字节——FDT 串恒 NUL 结,可直当 C 串用。 */
static const char *fdt_prop_str(const unsigned char *node, const char *pname)
{
    const unsigned char *p = node;
    while (p < fdt_dt_end) {
        unsigned int tok = fdt_be32(p);
        p += 4;
        if (tok != FDT_TOK_PROP)
            return 0;
        {
            unsigned int len = fdt_be32(p);
            const char *nm = (const char *)fdt_str + fdt_be32(p + 4);
            const char *data = (const char *)(p + 8);
            p += 8 + ((len + 3) & ~3u);
            if (len > 0 && fdt_streq(nm, pname))
                return data;
        }
    }
    return 0;
}

static void fdt_check(void)
{
    static const struct { const char *path; const char *tag; } probes[] = {
        { "/cpus/cpu@0",         "cpu" },
        { "/pl011@9000000",      "uart" },
        { "/virtio_mmio@a000000", "blk" },
    };
    unsigned int i;

    if (!fdt_init((const void *)FDT_ADDR)) {
        puts_uart("fdt FAIL: bad blob\n");
        return;
    }
    for (i = 0; i < sizeof(probes) / sizeof(probes[0]); i++) {
        const unsigned char *node = fdt_find(probes[i].path);
        const char *comp = node ? fdt_prop_str(node, "compatible") : 0;
        if (!comp) {
            puts_uart("fdt FAIL: ");
            puts_uart(probes[i].tag);
            puts_uart("\n");
            return;
        }
        puts_uart("fdt ok: ");
        puts_uart(probes[i].tag);
        puts_uart(" ");
        puts_uart(comp);
        puts_uart("\n");
    }
}

#endif /* ESHINE_GUEST_FDT_H */
