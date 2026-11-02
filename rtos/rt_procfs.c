/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "rtos.h"

/* 站四十:procfs——第六个 resmgr 服务器(服务收编段)。参考实现机制
 * 白描:procfs 服务=/proc 统一层(任务表可见性)。站 39 READDIR
 * 游标协议的第二消费者(非 ramfs 一家)——判据面互为回归。
 *
 * 语义:OPEN=目录把手(服务器侧 ocb 持行游标);READ=任务表文本
 * 行 "name prio state"(一次一行,64B 窗);READDIR=活任务名
 * (站38 律:TASK_LIVE 状态谓词,非 t->entry 时代错位);STAT=DIR。
 * 任务表经 rt_pool_base 只读出访(池基出口,站 28 守卫同源)。 */

#define PF_OCB_MAX 4u

/* 站50:OPEN 按路径分型(参考实现 seq_file 行游标站内形) */
enum { PF_T_TASK = 0, PF_T_MEM, PF_T_CPU, PF_T_IRQ };

struct pf_ocb {
    u32 used;
    u32 type;                    /* PF_T_*(TASK=既有任务表面) */
    u32 off;                     /* READ 行游标(READDIR 游标在客户端) */
};
static struct pf_ocb pf_ocbs[PF_OCB_MAX];

/* 手搓数字尾:u64 十进制(str 后拼;站50 procfs 生成行共用) */
static u32 pf_num(char *dst, u32 cap, u32 used, u64 v)
{
    char d[24];
    u32 n = 0, i = used;

    if (!v)
        d[n++] = '0';
    while (v && n < sizeof(d))
        { d[n++] = (char)('0' + (v % 10)); v /= 10; }
    while (n && i + 1 < cap)
        dst[i++] = d[--n];
    return i;
}

static u32 pf_str(char *dst, u32 cap, u32 used, const char *s)
{
    u32 i = used;

    while (s && *s && i + 1 < cap)
        dst[i++] = *s++;
    return i;
}

/* 任务名枚举与文本行生成共用一走查:cursor=活任务序,命中返基指针,
 * 出参 next=下一游标(回执兑付)。 */
static rt_task_t *pf_task_live(u32 cursor, u32 *out_next)
{
    rt_task_t *pool = rt_pool_base();
    u32 live = 0, i;

    for (i = 0; i < RT_POOL_MAX; i++) {
        if (pool[i].state == RT_FREE)
            continue;
        if (live == cursor) {
            *out_next = live + 1;
            return &pool[i];
        }
        live++;
    }
    *out_next = live;
    return NULL;
}

/* 手搓行(无 snprintf 面):"name prio STS\n"——判据锚用子串面 */
static u32 pf_line(char *dst, u32 cap, rt_task_t *t)
{
    const char *st = t->state == RT_RUN ? "RUN\n" : "BLK\n";
    u32 i, j;

    for (i = 0; i + 1 < cap && t->name[i]; i++)
        dst[i] = t->name[i];
    if (i + 1 >= cap)
        return 0;
    dst[i++] = ' ';
    {   /* prio 十进制 */
        char d[4];
        u32 n = 0, p = t->prio;

        if (!p)
            d[n++] = '0';
        while (p && n < 4)
            d[n++] = (char)('0' + p % 10), p /= 10;
        while (n && i + 1 < cap)
            dst[i++] = d[--n];
    }
    dst[i++] = ' ';
    for (j = 0; st[j] && i + 1 < cap; j++)
        dst[i++] = st[j];
    return i;
}

static void srv_procfs(void *arg)
{
    struct rt_fdreq req;
    struct rt_recvinfo ri;
    char out[RT_FD_RDATA];
    int chid;
    long r;

    (void)arg;
    chid = rt_msg_ch_create(0);
    if (chid <= 0)
        return;
    if (rt_msg_name_attach("/proc", chid) != 0) {
        rt_msg_ch_destroy(chid);
        return;
    }
    for (;;) {
        if (rt_msg_recv(chid, &req, sizeof(req), RT_WAIT_FOREVER, &ri) != 0)
            continue;
        if (ri.rcvid == 0)
            continue;
        r = -22;
        switch (req.op) {
        case RT_FDOP_OPEN: {
            /* 站50:路径分型——/proc=任务表(既有),/proc/meminfo
             * /proc/cpuinfo /proc/interrupts=生成文件(行游标) */
            u32 i;
            u32 type = PF_T_TASK;

            if (req.data[0] == '/' && req.data[1] == 'p' &&
                req.data[2] == 'r' && req.data[3] == 'o' &&
                req.data[4] == 'c') {
                const char *sub = req.data + 5;

                if (sub[0] == '\0' || (sub[0] == '/' && sub[1] == '\0'))
                    type = PF_T_TASK;
                else if (sub[0] == '/' && sub[1] == 'm' &&
                         sub[2] == 'e' && sub[3] == 'm')
                    type = PF_T_MEM;
                else if (sub[0] == '/' && sub[1] == 'c' &&
                         sub[2] == 'p' && sub[3] == 'u')
                    type = PF_T_CPU;
                else if (sub[0] == '/' && sub[1] == 'i' &&
                         sub[2] == 'n' && sub[3] == 't')
                    type = PF_T_IRQ;
                else {
                    r = -2;               /* -ENOENT */
                    break;
                }
            }
            for (i = 0; i < PF_OCB_MAX; i++)
                if (!pf_ocbs[i].used) {
                    pf_ocbs[i].used = 1;
                    pf_ocbs[i].type = type;
                    pf_ocbs[i].off = 0;
                    r = (long)i + 1;
                    break;
                }
            if (r == -22)
                r = -12;
            break;
        }
        case RT_FDOP_READ: {
            /* ocb->off=行号:一任务一行,EOF=status 0;
             * 站50 生成型=按 type 逐行生成(行游标同语义) */
            struct pf_ocb *o = req.ocb && req.ocb <= PF_OCB_MAX &&
                pf_ocbs[req.ocb - 1].used ? &pf_ocbs[req.ocb - 1] : NULL;
            rt_task_t *t;
            u32 next, n;
            u32 cap = req.a < RT_FD_RDATA ? req.a : RT_FD_RDATA;

            if (!o)
                break;
            if (o->type == PF_T_MEM) {
                /* 页池账:total/used/free kB(2048 页×4KB=8MB,
                 * 站40 容量账同源);首版 tot*4/4 雷被编译期眼过抓 */
                u32 used = rt_as_pages_used();
                u32 tot = 2048u;

                if (o->off == 0)
                    n = pf_num(out, cap, pf_str(out, cap, 0,
                              "MemTotal:  "), tot * 4u);
                else if (o->off == 1)
                    n = pf_num(out, cap, pf_str(out, cap, 0,
                              "MemFree:   "), (tot - used) * 4u);
                else if (o->off == 2)
                    n = pf_num(out, cap, pf_str(out, cap, 0,
                              "MemUsed:   "), used * 4u);
                else
                    break;                /* EOF */
                o->off++;
                rt_msg_reply(chid, ri.rcvid, out, n, (long)n);
                continue;
            }
            if (o->type == PF_T_CPU) {
                if (o->off >= 2u)
                    break;                /* 双核(vcpu0/vcpu1),EOF */
                n = pf_num(out, cap, pf_str(out, cap, 0,
                          "processor\t: "), o->off);
                o->off++;
                rt_msg_reply(chid, ri.rcvid, out, n, (long)n);
                continue;
            }
            if (o->type == PF_T_IRQ) {
                u32 intid;
                u64 cnt;

                if (!rt_irq_entry(o->off, &intid, &cnt))
                    break;                /* 末,EOF */
                if (intid == 27UL)
                    n = pf_num(out, cap, pf_str(out, cap, 0,
                              " 27: "), cnt);
                else if (intid == 33UL)
                    n = pf_num(out, cap, pf_str(out, cap, 0,
                              " 33: "), cnt);
                else if (intid == 35UL)
                    n = pf_num(out, cap, pf_str(out, cap, 0,
                              " 35: "), cnt);
                else if (intid == 0UL)
                    n = pf_num(out, cap, pf_str(out, cap, 0,
                              "  0: "), cnt);
                else
                    n = pf_num(out, cap, pf_str(out, cap, 0,
                              " ?? : "), cnt);
                o->off++;
                rt_msg_reply(chid, ri.rcvid, out, n, (long)n);
                continue;
            }
            t = pf_task_live(o->off, &next);
            if (!t)
                break;                    /* status 0=EOF */
            n = pf_line(out, cap, t);
            o->off = next;
            rt_msg_reply(chid, ri.rcvid, out, n, (long)n);
            continue;
        }
        case RT_FDOP_READDIR: {
            /* data[0]=d_type(REG),data[1..]=任务名;status=下一游标 */
            rt_task_t *t;
            u32 next, i;

            t = pf_task_live(req.a, &next);
            if (!t)
                break;                    /* status 0=末 */
            out[0] = (char)4u;            /* DT_REG(v0:名面无型) */
            for (i = 1; i < RT_FD_RDATA && t->name[i - 1]; i++)
                out[i] = t->name[i - 1];
            if (i < RT_FD_RDATA)
                out[i++] = '\0';
            rt_msg_reply(chid, ri.rcvid, out, i, (long)next);
            continue;
        }
        case RT_FDOP_STAT:
            /* 站50:按路径分型——meminfo/cpuinfo/interrupts=REG,
             * /proc 本体=DIR(任务表);参考实现 28 文件面的站内三件) */
            if (req.data[5] == '/' && req.data[6] == 'm')
                r = ((long)0x81A4u) << 32;    /* S_IFREG|0644,size 0 */
            else if (req.data[5] == '/' && req.data[6] == 'c')
                r = ((long)0x81A4u) << 32;
            else if (req.data[5] == '/' && req.data[6] == 'i')
                r = ((long)0x81A4u) << 32;
            else
                r = ((long)0x41EDu) << 32;    /* S_IFDIR|0755,size 0 */
            break;
        case RT_FDOP_CLOSE: {
            struct pf_ocb *o = req.ocb && req.ocb <= PF_OCB_MAX &&
                pf_ocbs[req.ocb - 1].used ? &pf_ocbs[req.ocb - 1] : NULL;

            if (o)
                o->used = 0;
            r = 0;
            break;
        }
        case RT_FDOP_DUP:
            r = 0;
            break;
        default:
            r = -22;
        }
        rt_msg_reply(chid, ri.rcvid, NULL, 0, r);
    }
}

void rt_procfs_boot(void)
{
    if (!rt_task_create("procsrv", srv_procfs, NULL, 8))
        rt_puts("srv: procsrv spawn failed\n");
}
