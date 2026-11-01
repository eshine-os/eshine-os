/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "rtos.h"

/* 站四十六:崩溃取证链——参考实现 coredump 白描的站内形状(ELF 核
 * 文件不可为:ramfs=只读 rodata 服务器;内存记录+shell 倾倒=v0
 * 诚实形,spec §4 记档)。
 *
 * 记录点两处:EL0 野访终态(硬件三元全,站29"全系统 panic"留边
 * 翻案=死者本人 exit 链)/信号默认动作(esr/elr/far=0,code=
 * 128+sig)。用户帧链=死者帧 x29 起走,窗门+**映射门**(逐读前
 * l2 PTE present 查——坏栈不得把取证变成 EL1 二次事故)+单调
 * 门,4 帧保险丝。 */

#define RT_CRASH_MAX    2u
#define RT_CRASH_UWALK  4u

/* 用户窗门:identity 块 1 ∪ mmap 窗块 6(与 as_map_page 的
 * 可映射域一致;越窗即止=站43 内核窗门的用户镜像)。 */
#define CR_U_LO 0x40200000ULL
#define CR_U_HI 0x40400000ULL
#define CR_M_LO 0x40C00000ULL
#define CR_M_HI 0x40E00000ULL

static int cr_in_user_window(u64 va)
{
    return (va >= CR_U_LO && va < CR_U_HI) ||
           (va >= CR_M_LO && va < CR_M_HI);
}

struct rt_crash {
    u32 used;
    u32 tick;
    int pid;
    char name[12];
    u64 esr, elr, far;               /* 信号路径=0(无硬件三元) */
    u32 code;                        /* 128+sig;野访=139 */
    u64 x[7];                        /* x0-x5+x29 寄存器面
                                      * (x29=用户帧链根,诊断可见) */
    u64 pc[RT_CRASH_UWALK];          /* 用户 x29 链前 4 帧 */
    u32 nframes;
};

static struct rt_crash crash_ring[RT_CRASH_MAX];
static u32 crash_next;

/* 记录者语境=死者本人(页表在役,用户栈仍可读;sys_exit 在
 * 本函数之后才回收)。frame=272B 帧(FR 布局:x0@16,x_i@
 * 24+8i,x29=24+8*29)。 */
static void crash_user_walk(struct rt_proc *p, u64 *frame,
                            struct rt_crash *c)
{
    u64 fp = frame[3 + 29];          /* x29 槽:x_i=frame[3+i](i>=1,
                                     * 字节 24+8*29=帧基+256) */
    u32 n = 0;

    while (n < RT_CRASH_UWALK && cr_in_user_window(fp) &&
           rt_as_user_mapped(p, fp) && rt_as_user_mapped(p, fp + 8)) {
        u64 ret = *(u64 *)(void *)(fp + 8);
        u64 nxt = *(u64 *)(void *)fp;

        if (!cr_in_user_window(ret))
            break;                   /* 出用户窗:libc/向量帧止步 */
        c->pc[n++] = ret;
        if (nxt <= fp || nxt - fp > 0x10000)
            break;                   /* 栈底/环/坏栈保险丝 */
        fp = nxt;
    }
    c->nframes = n;
}

void rt_crash_record(struct rt_proc *p, u64 *frame,
                     u64 esr, u64 elr, u64 far, u32 code)
{
    struct rt_crash *c = &crash_ring[crash_next % RT_CRASH_MAX];
    u32 i;

    crash_next++;
    c->used = 1;
    c->tick = (u32)rt_ticks;
    c->pid = p ? p->pid : -1;
    c->esr = esr;
    c->elr = elr;
    c->far = far;
    c->code = code;
    c->x[0] = frame[2];               /* x0@16;x_i(i>=1)=frame[3+i] */
    for (i = 1; i < 6; i++)
        c->x[i] = frame[3 + i];
    c->x[6] = frame[3 + 29];          /* x29:帧链根 */
    for (i = 0; i < 12 && rt_cur->name[i]; i++)
        c->name[i] = rt_cur->name[i];
    c->name[i] = '\0';
    c->nframes = 0;
    crash_user_walk(p, frame, c);
}

void rt_crash_show(void)
{
    u32 i, j;

    rt_puts_u64("crash: ", crash_next, " record(s), ring ");
    rt_puts_u64("", (u64)RT_CRASH_MAX, "\n");
    for (i = 0; i < RT_CRASH_MAX; i++) {
        struct rt_crash *c = &crash_ring[i];

        if (!c->used)
            continue;
        rt_puts_u64("crash[", i, "]: pid ");
        rt_puts_u64("", (u64)(u32)c->pid, " ");
        rt_puts(c->name);
        rt_puts_u64(" code ", c->code, " tick ");
        rt_puts_u64("", c->tick, "\n");
        if (c->far || c->esr) {
            rt_puts_hex("  esr 0x", c->esr, "");
            rt_puts_hex(" elr 0x", c->elr, "");
            rt_puts_hex(" far 0x", c->far, "\n");
        } else {
            rt_puts("  (signal path, no hw triple)\n");
        }
        for (j = 0; j < c->nframes; j++) {
            rt_puts_hex("  #", j, " pc=0x");
            rt_puts_hex("", c->pc[j], "\n");
        }
        rt_puts_u64("  x0 0x", c->x[0], "");
        rt_puts_u64(" x1 0x", c->x[1], "");
        rt_puts_u64(" x2 0x", c->x[2], "");
        rt_puts_u64(" x29 0x", c->x[6], "\n");
    }
}
