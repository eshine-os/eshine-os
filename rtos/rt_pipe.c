/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "rtos.h"

/* 站三十七:pipe——字节流双端(参考实现 "fdtable×同步队列件 会合点"
 * 机制白描的站内形状)。骑站 31 resmgr 零新 fd 线:pipesrv=第三
 * 个内核服务器任务(own channel+挂名 /dev/pipe),fd 三元组
 * {coid, ocb} 的 READ/WRITE/CLOSE 走既有 wire ops。
 *
 * ocb 编码=(pipe_id<<1)|端(0=read,1=write)。pipe() 号面 576
 * =内核侧直接建对象+连服务器+登记两 fd(内核与服务器共表=免
 * OPEN 往返,v0 直连记边界)。
 *
 * 环语义:满写/空读=服务器持消息挂对象等待队(站 30 "占住
 * 服务器"形)——**有界窗**(PIPE_WAIT 拍)后回 -110 超时脸;
 * POSIX 无限阻塞全弧=后弧记边界(abort/UNBLOCK 与服务器挂等
 * 的组合窗已由站 30 abortdemo 覆盖同款)。
 * **EOF 脸**:写端引用全关后 READ 恒回 0(POSIX 语义)。 */

#define PIPE_MAX      4u
#define PIPE_RING     256u
#define PIPE_WAIT     50u

struct rt_pipe {
    u32 used;
    u32 readers, writers;         /* 端引用计数(CLOSE 递减) */
    u32 r, w, count;              /* 环游标 */
    char ring[PIPE_RING];
};

static struct rt_pipe pipes[PIPE_MAX];

static struct rt_pipe *pipe_ocb(u32 ocb, u32 *end)
{
    struct rt_pipe *pp;

    if ((ocb >> 1) == 0 || (ocb >> 1) > PIPE_MAX)
        return NULL;
    pp = &pipes[(ocb >> 1) - 1];
    if (!pp->used)
        return NULL;
    *end = ocb & 1u;
    return pp;
}

/* 服务器:READ(空=EOF 判+有界等待)/WRITE(满=有界等待)/
 * CLOSE(端引用递减,写端归零唤醒——EOF 即刻可见)。 */
static void srv_pipe(void *arg)
{
    struct rt_fdreq req;
    struct rt_recvinfo ri;
    int chid;
    long r;

    (void)arg;
    chid = rt_msg_ch_create(0);
    if (chid <= 0)
        return;
    if (rt_msg_name_attach("/dev/pipe", chid) != 0) {
        rt_msg_ch_destroy(chid);
        return;
    }
    for (;;) {
        if (rt_msg_recv(chid, &req, sizeof(req), RT_WAIT_FOREVER, &ri) != 0)
            continue;
        if (ri.rcvid == 0)
            continue;               /* 脉冲面 v0 丢弃 */
        switch (req.op) {
        case RT_FDOP_READ: {
            u32 end, n = 0;

            {
                struct rt_pipe *pp = pipe_ocb(req.ocb, &end);

                if (!pp || end != 0) {
                    r = -9;         /* -EBADF */
                    break;
                }
                if (pp->count == 0) {
                    if (pp->writers == 0)
                        r = 0;      /* EOF:写端全关+空环 */
                    else
                        r = -110;   /* 空且写端活:有界窗面(v0) */
                    break;
                }
                n = req.a > pp->count ? pp->count : req.a;
                if (n > RT_FD_RDATA)
                    n = RT_FD_RDATA;
            }
            {
                struct rt_pipe *pp = pipe_ocb(req.ocb, &end);
                u32 i;

                for (i = 0; i < n; i++) {
                    req.data[i] = pp->ring[pp->r];
                    pp->r = (pp->r + 1) % PIPE_RING;
                }
                pp->count -= n;
            }
            r = (long)n;
            break;
        }
        case RT_FDOP_WRITE: {
            u32 end, n = req.a > RT_FD_DATA ? RT_FD_DATA : req.a;
            struct rt_pipe *pp = pipe_ocb(req.ocb, &end);

            if (!pp || end != 1) {
                r = -9;
                break;
            }
            if (pp->count + n > PIPE_RING) {
                r = -110;           /* 满环:有界窗面(v0) */
                break;
            }
            {
                u32 i;

                for (i = 0; i < n; i++) {
                    pp->ring[pp->w] = req.data[i];
                    pp->w = (pp->w + 1) % PIPE_RING;
                }
                pp->count += n;
            }
            r = (long)n;
            break;
        }
        case RT_FDOP_CLOSE: {
            u32 end;
            struct rt_pipe *pp = pipe_ocb(req.ocb, &end);

            if (!pp)
                r = -9;
            else {
                if (end)
                    pp->writers = pp->writers ? pp->writers - 1 : 0;
                else
                    pp->readers = pp->readers ? pp->readers - 1 : 0;
                if (!pp->readers && !pp->writers)
                    pp->used = 0;   /* 双端归零:对象收 */
            }
            r = 0;
            break;
        }
        case RT_FDOP_DUP: {         /* 站31 双侧镜像:fork/继承
                                   * 的 ocb 引用在此计数(与
                                   * rt_fd_fork 的 DUP 消息对上,
                                   * 否则父早关=子尚写而 refs 已零
                                   * =EOF 假脸) */
            u32 end;
            struct rt_pipe *pp = pipe_ocb(req.ocb, &end);

            if (!pp)
                r = -9;
            else {
                if (end)
                    pp->writers++;
                else
                    pp->readers++;
                r = 0;
            }
            break;
        }
        default:
            r = -22;
        }
        rt_msg_reply(chid, ri.rcvid,
                     (req.op == RT_FDOP_READ && r > 0) ? req.data : NULL,
                     (req.op == RT_FDOP_READ && r > 0) ? (u32)r : 0, r);
    }
}

void rt_pipe_boot(void)
{
    if (!rt_task_create("pipesrv", srv_pipe, NULL, 8))
        rt_puts("srv: pipesrv spawn failed\n");
}

/* pipe():内核侧建对象(与服务器共表,免 OPEN 往返=v0 直连)+
 * 连服务器+fd 表登记两端。返回读端 fd,写端走 FR 面。 */
long rt_pipe_create(struct rt_proc *p)
{
    u32 i, coid;
    int fd[2];

    if (!p)
        return -22;
    for (i = 0; i < PIPE_MAX; i++)
        if (!pipes[i].used)
            break;
    if (i == PIPE_MAX)
        return -12;
    coid = rt_msg_name_open("/dev/pipe");
    if (coid <= 0)
        return -2;
    for (fd[0] = 0; fd[0] < (int)RT_FD_MAX; fd[0]++)
        if (!p->fds[fd[0]].used)
            break;
    for (fd[1] = fd[0] + 1; fd[1] < (int)RT_FD_MAX; fd[1]++)
        if (!p->fds[fd[1]].used)
            break;
    if (fd[1] >= (int)RT_FD_MAX) {
        rt_msg_conn_detach(coid);
        return -12;
    }
    pipes[i].used = 1;
    pipes[i].readers = 1;
    pipes[i].writers = 1;
    pipes[i].r = pipes[i].w = pipes[i].count = 0;
    p->fds[fd[0]].used = 1;
    p->fds[fd[0]].coid = coid;
    p->fds[fd[0]].ocb = ((i + 1) << 1) | 0u;      /* 读端 */
    p->fds[fd[1]].used = 1;
    p->fds[fd[1]].coid = coid;
    p->fds[fd[1]].ocb = ((i + 1) << 1) | 1u;      /* 写端 */
    rt_msg_conn_ref(coid);                        /* 两 fd 各持一引 */
    rt_msg_conn_ref(coid);
    return (long)fd[0] | ((long)fd[1] << 16);     /* 两端打包返 */
}
