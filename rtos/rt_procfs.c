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

struct pf_ocb {
    u32 used;
    u32 off;                     /* READ 行游标(READDIR 游标在客户端) */
};
static struct pf_ocb pf_ocbs[PF_OCB_MAX];

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
            u32 i;

            for (i = 0; i < PF_OCB_MAX; i++)
                if (!pf_ocbs[i].used) {
                    pf_ocbs[i].used = 1;
                    pf_ocbs[i].off = 0;
                    r = (long)i + 1;
                    break;
                }
            if (r == -22)
                r = -12;
            break;
        }
        case RT_FDOP_READ: {
            /* ocb->off=行号:一任务一行,EOF=status 0 */
            struct pf_ocb *o = req.ocb && req.ocb <= PF_OCB_MAX &&
                pf_ocbs[req.ocb - 1].used ? &pf_ocbs[req.ocb - 1] : NULL;
            rt_task_t *t;
            u32 next, n;

            if (!o)
                break;
            t = pf_task_live(o->off, &next);
            if (!t)
                break;                    /* status 0=EOF */
            n = pf_line(out, req.a < RT_FD_RDATA ? req.a : RT_FD_RDATA, t);
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
