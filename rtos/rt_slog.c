/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "rtos.h"

/* 站四十:slogd——第七个 resmgr 服务器(服务收编段)。参考实现机制
 * 白描:系统日志服务=环形缓冲+级别策略+消费面。站内形状:fd 面
 * ——WRITE=入环(data[0]=级别,data[1..]=文本),READ=环倾倒
 * (ocb.off=环位,一次一行);策略:warn 及以上控制台回显
 * (tty 面 v0 走 rt_puts=内核语境串口)。 */

#define SLOG_RING 16u
#define SLOG_LINE 44u
#define SLOG_WARN 2u

struct slog_slot {
    u32 len;                     /* 文本长(不含 NUL) */
    char level;                  /* 0=info 1=notice 2=warn 3=err */
    char text[SLOG_LINE];
};
static struct slog_slot slog_ring[SLOG_RING];
static u32 slog_head;            /* 下一写入位(环) */
static u32 slog_count;

#define SLOG_OCB_MAX 4u
struct slog_ocb {
    u32 used;
    u32 off;                     /* READ=环位(自 0 顺游) */
};
static struct slog_ocb slog_ocbs[SLOG_OCB_MAX];

static const char slog_ch[] = { '?', 'N', 'W', 'E' };  /* 级别字 */

static void srv_slog(void *arg)
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
    if (rt_msg_name_attach("/dev/slog", chid) != 0) {
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

            for (i = 0; i < SLOG_OCB_MAX; i++)
                if (!slog_ocbs[i].used) {
                    slog_ocbs[i].used = 1;
                    slog_ocbs[i].off = 0;
                    r = (long)i + 1;
                    break;
                }
            if (r == -22)
                r = -12;
            break;
        }
        case RT_FDOP_WRITE: {
            /* 入环:data[0]=级别,data[1..]=文本(req.a=窗长含级别) */
            struct slog_slot *s;
            u32 n = req.a, i;

            if (n == 0 || n > RT_FD_DATA)
                break;
            s = &slog_ring[slog_head];
            s->level = (char)(req.data[0] & 3);
            s->len = n - 1 > SLOG_LINE - 1 ? SLOG_LINE - 1 : n - 1;
            for (i = 0; i < s->len; i++)
                s->text[i] = req.data[1 + i];
            s->text[s->len] = '\0';
            if (slog_count < SLOG_RING)
                slog_count++;
            if ((u32)s->level >= SLOG_WARN) {   /* 策略:warn+ 回显(单行) */
                char echo[SLOG_LINE + 12];
                u32 j = 0;

                echo[j++] = 's'; echo[j++] = 'l'; echo[j++] = 'o';
                echo[j++] = 'g'; echo[j++] = ':'; echo[j++] = ' ';
                echo[j++] = '['; echo[j++] = slog_ch[(u32)s->level & 3];
                echo[j++] = ']'; echo[j++] = ' ';
                for (i = 0; i < s->len; i++)
                    echo[j++] = s->text[i];
                echo[j++] = '\n';
                rt_puts_n(echo, j);
            }
            slog_head = (slog_head + 1) % SLOG_RING;
            r = (long)n;
            break;
        }
        case RT_FDOP_READ: {
            /* 倾倒:ocb.off=环序(旧到新),一行 "[X] text" */
            struct slog_ocb *o = req.ocb && req.ocb <= SLOG_OCB_MAX &&
                slog_ocbs[req.ocb - 1].used ? &slog_ocbs[req.ocb - 1] : NULL;
            struct slog_slot *s;
            u32 i, n = 0;

            if (!o)
                break;
            if (o->off >= slog_count)
                break;                    /* status 0=EOF */
            s = &slog_ring[(slog_head + SLOG_RING - slog_count +
                            o->off) % SLOG_RING];
            out[n++] = '[';
            out[n++] = slog_ch[(u32)s->level & 3];
            out[n++] = ']';
            out[n++] = ' ';
            for (i = 0; i < s->len && n < RT_FD_RDATA - 1; i++)
                out[n++] = s->text[i];
            out[n++] = '\n';
            o->off++;
            rt_msg_reply(chid, ri.rcvid, out, n, (long)n);
            continue;
        }
        case RT_FDOP_STAT:
            r = ((long)0x81A4u) << 32;    /* S_IFCHR|0644,size 0 */
            break;
        case RT_FDOP_CLOSE: {
            struct slog_ocb *o = req.ocb && req.ocb <= SLOG_OCB_MAX &&
                slog_ocbs[req.ocb - 1].used ? &slog_ocbs[req.ocb - 1] : NULL;

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

void rt_slog_boot(void)
{
    if (!rt_task_create("slogsrv", srv_slog, NULL, 8))
        rt_puts("srv: slogsrv spawn failed\n");
}
