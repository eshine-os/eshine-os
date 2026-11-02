/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "rtos.h"

/* 站五十七:AF_UNIX socket 服务(参考实现 unix_sock 白描的站内形,
 * 批次 C 首站)。socksrv=第九个内核服务器任务(own channel+挂名
 * /dev/sock 元名);fd 三元组 {coid, ocb} 骑站 31 wire 面:
 * READ=recv/READ 面,WRITE=send/WRITE 面(数据面零新 op);控制面
 * 新 wire ops(SOCKET/BIND/LISTEN/ACCEPT/CONNECT/SHUTDOWN)。
 *
 * 语义:bind=对象挂路径(服务器内部表);connect=按路径寻听者->
 * 双 socket 即刻配对(accepted socket 预建入听者 accept 队列=
 * backlog 语义:连接在 connect 时成立,accept 只出队);数据=双
 * socket 各持 rx 环,send 写对端环+recv 读本端环;空读/空 accept=
 * -110 有界窗面(pipesrv v0 同形,POSIX 无限阻塞=后弧;消费者
 * 重试环自持)。
 * ocb=sock_id+1。真号面:socket 198/bind 200/listen 201/accept
 * 202/connect 203/sendto 206/recvfrom 207/shutdown 210(aarch64
 * asm-generic;站 42 实测定号纪律——musl 直连零 patch 前提)。 */

#define SOCK_MAX 6u
#define SOCK_RX 128u
#define SOCK_PATH 24u

struct rt_sock {
    u32 used;
    u32 refs;                      /* fd 引用计数(fork DUP 镜像,pipe 同款) */
    u32 state;                    /* 0=idle 1=listening 2=connected */
    u32 peer;                      /* 对端 sock_id+1(0=无) */
    char path[SOCK_PATH];         /* bind 挂名(听者) */
    u32 r, w, count;               /* rx 环游标 */
    char rx[SOCK_RX];
    u32 pend_head, pend_tail;      /* accept 队(环 2 深) */
    u32 pend[2];                   /* 待 accept 的 accepted socket id+1 */
};

static struct rt_sock socks[SOCK_MAX];

static struct rt_sock *sock_ocb(u32 ocb)
{
    if (ocb == 0 || ocb > SOCK_MAX)
        return NULL;
    return socks[ocb - 1].used ? &socks[ocb - 1] : NULL;
}

static struct rt_sock *sock_alloc(void)
{
    u32 i;

    for (i = 0; i < SOCK_MAX; i++)
        if (!socks[i].used) {
            socks[i].used = 1;
            socks[i].refs = 1;
            socks[i].state = 0;
            socks[i].peer = 0;
            socks[i].path[0] = '\0';
            socks[i].r = socks[i].w = socks[i].count = 0;
            socks[i].pend_head = socks[i].pend_tail = 0;
            return &socks[i];
        }
    return NULL;
}

static void srv_sock(void *arg)
{
    struct rt_fdreq req;
    struct rt_recvinfo ri;
    int chid;
    long r;

    (void)arg;
    chid = rt_msg_ch_create(0);
    if (chid <= 0)
        return;
    if (rt_msg_name_attach("/dev/sock", chid) != 0) {
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
        case RT_FDOP_SOCKET: {
            struct rt_sock *sk = sock_alloc();

            r = sk ? (long)(u32)((sk - socks) + 1) : -12;
            break;
        }
        case RT_FDOP_BIND: {
            struct rt_sock *sk = sock_ocb(req.ocb);
            u32 i;

            if (!sk) {
                r = -9;
                break;
            }
            for (i = 0; i + 1 < SOCK_PATH && i + 1 < RT_FD_DATA &&
                        req.data[i]; i++)
                sk->path[i] = req.data[i];
            sk->path[i] = '\0';
            r = 0;
            break;
        }
        case RT_FDOP_LISTEN: {
            struct rt_sock *sk = sock_ocb(req.ocb);

            if (!sk) {
                r = -9;
                break;
            }
            sk->state = 1;
            r = 0;                 /* backlog v0 恒队深 2 */
            break;
        }
        case RT_FDOP_CONNECT: {
            struct rt_sock *sk = sock_ocb(req.ocb);
            struct rt_sock *ls = NULL;
            u32 i;

            if (!sk) {
                r = -9;
                break;
            }
            for (i = 0; i < SOCK_MAX; i++)
                if (socks[i].used && socks[i].state == 1) {
                    u32 j = 0;

                    while (socks[i].path[j] && j < SOCK_PATH &&
                           j + 1 < RT_FD_DATA && req.data[j] &&
                           socks[i].path[j] == req.data[j])
                        j++;
                    if (req.data[j] == '\0' && socks[i].path[j] == '\0') {
                        ls = &socks[i];
                        break;
                    }
                }
            if (!ls) {
                r = -2;            /* -ENOENT:无此听者 */
                break;
            }
            {
                struct rt_sock *as = sock_alloc();

                if (!as) {
                    r = -12;
                    break;
                }
                if (((ls->pend_tail + 1) & 1u) == ls->pend_head) {
                    as->used = 0;
                    r = -12;       /* accept 队满:拒连 */
                    break;
                }
                as->state = 2;
                as->peer = req.ocb;          /* peer=连接者 */
                sk->state = 2;
                sk->peer = (u32)(as - socks) + 1;
                ls->pend[ls->pend_tail] = (u32)(as - socks) + 1;
                ls->pend_tail = (ls->pend_tail + 1) & 1u;
            }
            r = 0;
            break;
        }
        case RT_FDOP_ACCEPT: {
            struct rt_sock *sk = sock_ocb(req.ocb);

            if (!sk || sk->state != 1) {
                r = sk ? -22 : -9;
                break;
            }
            if (sk->pend_head == sk->pend_tail) {
                r = -110;          /* 空队:有界窗面(重试环自持) */
                break;
            }
            r = (long)sk->pend[sk->pend_head];
            sk->pend_head = (sk->pend_head + 1) & 1u;
            break;
        }
        case RT_FDOP_READ: {       /* recv:读本端 rx 环 */
            struct rt_sock *sk = sock_ocb(req.ocb);
            u32 n;

            if (!sk || sk->state != 2) {
                r = sk ? -22 : -9;
                break;
            }
            if (sk->count == 0) {
                r = sk->peer ? -110 : 0;   /* 对端关=EOF;空=窗面 */
                break;
            }
            n = req.a > sk->count ? sk->count : req.a;
            if (n > RT_FD_RDATA)
                n = RT_FD_RDATA;
            {
                u32 i;

                for (i = 0; i < n; i++) {
                    req.data[i] = sk->rx[sk->r];
                    sk->r = (sk->r + 1) % SOCK_RX;
                }
                sk->count -= n;
            }
            rt_msg_reply(chid, ri.rcvid, req.data, n, (long)n);
            continue;
        }
        case RT_FDOP_WRITE: {      /* send:写对端 rx 环 */
            struct rt_sock *sk = sock_ocb(req.ocb);
            struct rt_sock *pe;
            u32 n = req.a > RT_FD_DATA ? RT_FD_DATA : req.a;

            if (!sk || sk->state != 2 || !sk->peer) {
                r = -9;
                break;
            }
            pe = sock_ocb(sk->peer);
            if (!pe) {
                r = -32;           /* -EPIPE */
                break;
            }
            if (pe->count + n > SOCK_RX) {
                r = -110;          /* 满环:窗面 */
                break;
            }
            {
                u32 i;

                for (i = 0; i < n; i++) {
                    pe->rx[pe->w] = req.data[i];
                    pe->w = (pe->w + 1) % SOCK_RX;
                }
                pe->count += n;
            }
            r = (long)n;
            break;
        }
        case RT_FDOP_POLL: {
            struct rt_sock *sk = sock_ocb(req.ocb);

            if (!sk)
                r = -9;
            else
                r = (long)(sk->count != 0 ? 1 : 0);
            break;
        }
        case RT_FDOP_SHUTDOWN: {
            struct rt_sock *sk = sock_ocb(req.ocb);

            if (!sk) {
                r = -9;
                break;
            }
            if (sk->peer) {        /* 对端 EOF:断对(单侧收尾对侧可见) */
                struct rt_sock *pe = sock_ocb(sk->peer);

                if (pe && pe->peer == (u32)(sk - socks) + 1)
                    pe->peer = 0;
                sk->peer = 0;
            }
            r = 0;
            break;
        }
        case RT_FDOP_DUP: {         /* fork 继承:引用+1(对齐 rt_fd_fork;
                                     * 不应答=子进程退出 CLOSE 早收对象) */
            struct rt_sock *sk = sock_ocb(req.ocb);

            if (!sk)
                r = -9;
            else
                sk->refs++, r = 0;
            break;
        }
        case RT_FDOP_CLOSE: {
            struct rt_sock *sk = sock_ocb(req.ocb);

            if (!sk) {
                r = -9;
                break;
            }
            if (sk->refs > 1) {
                sk->refs--;
                r = 0;
                break;
            }
            if (sk->peer) {
                struct rt_sock *pe = sock_ocb(sk->peer);

                if (pe && pe->peer == (u32)(sk - socks) + 1)
                    pe->peer = 0;  /* 对端 EOF 面 */
            }
            sk->used = 0;
            r = 0;
            break;
        }
        default:
            r = -22;
        }
        rt_msg_reply(chid, ri.rcvid, NULL, 0, r);
    }
}

/* ---- 内核侧客户端面(syscall 层调用;fd 三元组生命周期同 fd 面) ---- */

static int sock_coid(void)
{
    return rt_msg_name_open("/dev/sock");
}

int rt_fd_socket(struct rt_proc *p)
{
    struct rt_fdreq req;
    int coid, fd, r;

    if (!p)
        return -22;
    if (fd_used_count(p) >= p->max_fd)
        return -24;
    coid = sock_coid();
    if (coid < 0)
        return coid;
    fd_zero(&req, sizeof(req));
    req.op = RT_FDOP_SOCKET;
    r = (int)rt_msg_send(coid, &req, sizeof(req), NULL, 0, 100u);
    if (r < 0) {
        rt_msg_conn_detach(coid);
        return r;
    }
    for (fd = 0; fd < (int)RT_FD_MAX; fd++)
        if (!p->fds[fd].used)
            break;
    if (fd == (int)RT_FD_MAX) {
        rt_msg_conn_detach(coid);
        return -12;
    }
    p->fds[fd].used = 1;
    p->fds[fd].coid = coid;
    p->fds[fd].ocb = (u32)r;
    return fd;
}

static long sock_op(struct rt_proc *p, int fd, u32 op, u32 a, u32 b,
                    const char *path)
{
    struct rt_fdreq req;
    u32 i;

    if (!p || !fd_slot_ok(p, fd))
        return -9;
    fd_zero(&req, sizeof(req));
    req.op = op;
    req.ocb = p->fds[fd].ocb;
    req.a = a;
    req.rsv = b;
    if (path) {
        for (i = 0; i + 1 < RT_FD_DATA && path[i]; i++)
            req.data[i] = path[i];
        req.data[i] = '\0';
    }
    return rt_msg_send(p->fds[fd].coid, &req, sizeof(req), NULL, 0, 100u);
}

long rt_fd_bind(struct rt_proc *p, int fd, const char *upath)
{
    char kpath[SOCK_PATH];
    u32 i = 0;

    while (upath && upath[i] && i < sizeof(kpath) - 1) {
        kpath[i] = upath[i];
        i++;
    }
    kpath[i] = '\0';
    return sock_op(p, fd, RT_FDOP_BIND, 0, 0, kpath);
}

long rt_fd_listen(struct rt_proc *p, int fd, long backlog)
{
    return sock_op(p, fd, RT_FDOP_LISTEN, (u32)backlog, 0, NULL);
}

long rt_fd_connect(struct rt_proc *p, int fd, const char *upath)
{
    char kpath[SOCK_PATH];
    u32 i = 0;

    while (upath && upath[i] && i < sizeof(kpath) - 1) {
        kpath[i] = upath[i];
        i++;
    }
    kpath[i] = '\0';
    return sock_op(p, fd, RT_FDOP_CONNECT, 0, 0, kpath);
}

int rt_fd_accept(struct rt_proc *p, int fd)
{
    long r = sock_op(p, fd, RT_FDOP_ACCEPT, 0, 0, NULL);
    int nfd;

    if (r < 0)
        return (int)r;
    if (fd_used_count(p) >= p->max_fd)
        return -24;
    for (nfd = 0; nfd < (int)RT_FD_MAX; nfd++)
        if (!p->fds[nfd].used)
            break;
    if (nfd == (int)RT_FD_MAX)
        return -12;
    p->fds[nfd].used = 1;
    p->fds[nfd].coid = p->fds[fd].coid;
    p->fds[nfd].ocb = (u32)r;
    return nfd;
}

long rt_fd_shutdown(struct rt_proc *p, int fd, long how)
{
    return sock_op(p, fd, RT_FDOP_SHUTDOWN, (u32)how, 0, NULL);
}

void rt_sock_boot(void)
{
    if (!rt_task_create("socksrv", srv_sock, NULL, 8))
        rt_puts("srv: socksrv spawn failed\n");
}
