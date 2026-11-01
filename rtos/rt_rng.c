/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "rtos.h"
#include "timer.h"         /* vtimer_now:熵源搅入(站 21 时基) */

/* 站四十:random——第五个 resmgr 服务器(服务收编段)。参考实现机制
 * 白描:random 服务供 /dev/random|urandom 读写面;熵源在 v0 = 架构
 * 计数器抖动+调度噪声(非密码级,spec §3 记档;密码级熵=后弧)。
 *
 * 两脸同源混出器:xorshift64*(内核静态态,boot 时 cntvct 种入;
 * 每调用搅入 cntvct 低位=时基噪声)。fd READ 面(经三段消息)与
 * getrandom(278) 内核面都走 rt_rng_fill——同态同源,判据单点。 */

static u64 rng_state;

/* 混出:xorshift64* 三移+乘;每调用搅入 cntvct(不同调用天然
 * 异拍)+rt_ticks(调度可见性)。v0 诚实面:非密码级。 */
void rt_rng_fill(char *buf, u32 len)
{
    u64 x = rng_state ^ (rt_ticks << 16);
    u32 i;

    x ^= vtimer_now() & 0xffffULL;
    for (i = 0; i < len; i++) {
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        buf[i] = (char)(x * 2685821657736338717ULL >> 24);
    }
    rng_state = x * 6364136223846793005ULL + 1442695040888963407ULL;
}

/* 服务器:OPEN 立态(单一 ocb);READ=req.a 字节混出入窗回执。
 * 服务器=内核任务(站 30-39 同形),混出器即同址静态区——零跨面。 */
static void srv_random(void *arg)
{
    struct rt_fdreq req;
    struct rt_recvinfo ri;
    char out[RT_FD_RDATA];
    int chid;
    long r;

    (void)arg;
    rng_state = vtimer_now() | 1;
    chid = rt_msg_ch_create(0);
    if (chid <= 0)
        return;
    if (rt_msg_name_attach("/dev/random", chid) != 0) {
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
        case RT_FDOP_OPEN:
            r = 1;                       /* 单一 ocb:无每打开态 */
            break;
        case RT_FDOP_READ: {
            u32 n = req.a;

            if (n > RT_FD_RDATA)
                n = RT_FD_RDATA;
            rt_rng_fill(out, n);
            rt_msg_reply(chid, ri.rcvid, out, n, (long)n);
            continue;                     /* READ 走 rbuf 面自答自续 */
        }
        case RT_FDOP_CLOSE:
        case RT_FDOP_DUP:
            r = 0;
            break;
        default:
            r = -22;
        }
        rt_msg_reply(chid, ri.rcvid, NULL, 0, r);
    }
}

void rt_rng_boot(void)
{
    if (!rt_task_create("rngsrv", srv_random, NULL, 8))
        rt_puts("srv: rngsrv spawn failed\n");
}
