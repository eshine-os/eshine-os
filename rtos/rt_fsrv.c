/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "rtos.h"

/* 站三十一:server+rootfs——fd=通道三元组与用户态服务的会合。
 * 参考实现 §10 机制基线(fdtable 与 同步队列件 的会合点/QNX resmgr
 * 模型/开机链)的站内尺度收编,机制参考零代码拷贝:
 *
 *   fd 表(rt_proc.fds):本地名只是 {coid, ocb} 两字——路由面=
 *   连接实体(->服务器通道,站 30 六差距①),状态面=ocb(服务
 *   器侧每打开一份,引用计数=fork 继承的镜像)。
 *
 *   wire 契约(载荷 64B 内):请求 {op,a,ocb,rsv,data[44]}
 *   (OPEN=data 路径;WRITE=data 数据;READ=a 窗长);回复走站 30
 *   status 面(status 即 send 返回值),READ 的数据走 rbuf 面。
 *   DUP=fork 继承的服务器侧 ocb 引用+1(与客户端 coid 引用
 *   互为镜像——单方向只减一半会悬空:父先 CLOSE 释放 ocb,
 *   子的 fd 即指向已回收号,再开同号即位置串写)。
 *
 *   服务器 v0=内核任务直调 IPC 层(壳=EL0 用户客户端;"用户态
 *   server"二进制=站 33 musl 弧)。开机链=固定序列 tty->ramfs->pm
 *   (参考实现 PM 固定序列形状,F3-18 僵化审计记档,协商位留后):
 *   壳(5)先跑打横幅后阻塞,服务器(8)同优先级 FIFO 逐个起立
 *   挂名,PM 的 open 一次即中。
 *
 * rootfs=cpio(newc) 归档,构建期 tools/gen_ramfs.py 打包
 * tools/ramfs 树,rtos/ramfs.S .incbin 内嵌内核 .rodata(用户窗
 * 不可见——文件内容只经三段消息送达,这正是 fd=通道三元组
 * 的本义)。ramfs 服务器开机解析索引(PNODE)+逐路径挂名。 */

/* 站37:fd wire 契约(RT_FDOP 族+rt_fdreq+窗常量)移驻
 * rtos.h——跨文件契约(pipesrv 同吃);本文件沿用,
 * SRV_TICKS 与 CLOSE_TICKS 及 ocb 池仍驻此。 */

#define SRV_TICKS       100u   /* fd 层趟输窗(拍) */
#define CLOSE_TICKS     50u    /* 关闭/继承面窗(收窄:死服不拖尸) */
#define SRV_OCB_MAX     8u     /* POCB 池(tty+ramfs 共表;stdio 独占 3,
                                * motd/version 并开即贴 4 上限,放宽余量) */

/* ---- 通用小件(freestanding,无 libc) ---- */

static void fd_zero(void *dst, u32 n)
{
    char *d = dst;

    while (n--)
        *d++ = 0;
}

/* ---- POCB:每打开控制块(引用计数;参考实现 posix_queue 形状) ---- */

struct srv_ocb {
    u32 used;
    u32 refcnt;
    u32 node;                  /* ramfs:节点号;tty 恒 0 */
    u32 off;                   /* ramfs:文件位置(每打开态) */
};

static struct srv_ocb ocbs[SRV_OCB_MAX];

static int ocb_alloc(u32 node)
{
    u32 i;

    for (i = 0; i < SRV_OCB_MAX; i++) {
        if (!ocbs[i].used) {
            ocbs[i].used = 1;
            ocbs[i].refcnt = 1;
            ocbs[i].node = node;
            ocbs[i].off = 0;
            return (int)i + 1;         /* 1..N(0=非法) */
        }
    }
    return -12;
}

static struct srv_ocb *ocb_get(int id)
{
    if (id <= 0 || (u32)id > SRV_OCB_MAX || !ocbs[id - 1].used)
        return NULL;
    return &ocbs[id - 1];
}

static int ocb_ref(int id)
{
    struct srv_ocb *o = ocb_get(id);

    if (!o)
        return -22;
    o->refcnt++;
    return 0;
}

static int ocb_release(int id)
{
    struct srv_ocb *o = ocb_get(id);

    if (!o)
        return -22;
    if (--o->refcnt == 0)
        o->used = 0;
    return 0;
}

/* ---- tty 服务器(/dev/tty):字符设备 resmgr 面 ----
 * WRITE->rt_puts_n 落串口(fdwrite(1,..) 的全链);OPEN/DUP/CLOSE=
 * ocb 簿记;READ=v0 恒 EOF(stdin 走原始 sys_read 字符面)。 */

static void srv_tty(void *arg)
{
    struct rt_fdreq req;
    struct rt_recvinfo ri;
    int chid;
    long r;

    (void)arg;
    chid = rt_msg_ch_create(0);
    if (chid <= 0)
        return;
    if (rt_msg_name_attach("/dev/tty", chid) != 0) {
        rt_msg_ch_destroy(chid);
        return;
    }
    for (;;) {
        if (rt_msg_recv(chid, &req, sizeof(req), RT_WAIT_FOREVER, &ri) != 0)
            continue;                   /* 保守窗重臂(FOREVER 恒不达) */
        if (ri.rcvid == 0)
            continue;                   /* 脉冲面 v0 丢弃(记档) */
        switch (req.op) {
        case RT_FDOP_OPEN:
            r = (long)ocb_alloc(0);
            break;
        case RT_FDOP_WRITE: {
            u32 n = req.a > RT_FD_DATA ? RT_FD_DATA : req.a;

            rt_puts_n(req.data, n);     /* fd->通道->resmgr->串口 */
            r = (long)n;
            break;
        }
        case RT_FDOP_READ:
            r = 0;                      /* 恒 EOF(v0 边界诚实记档) */
            break;
        case RT_FDOP_CLOSE:
            r = ocb_release((int)req.ocb);
            break;
        case RT_FDOP_DUP:
            r = ocb_ref((int)req.ocb);
            break;
        case RT_FDOP_SEEK:
            r = -22;                   /* 字符面无 seek(ESPIPE 意) */
            break;
        default:
            r = -22;
        }
        rt_msg_reply(chid, ri.rcvid, NULL, 0, r);
    }
}

/* ---- ramfs 服务器:cpio(newc)解析+逐路径挂名+OPEN/READ/CLOSE ----
 * PNODE=静态索引(路径/数据偏移/尺寸);POCB=ocb{node,off}。
 * 归档名存相对名(cpio 通例),挂名补前导 '/'(name_attach 契约)。 */

extern const char rt_ramfs_start[];    /* rtos/ramfs.S(.incbin) */
extern const char rt_ramfs_end[];

#define RF_NODE_MAX 16u

struct rf_node {
    char path[48];                      /* 挂名形(含前导 '/') */
    u32 off;                            /* 数据偏移(自归档基) */
    u32 size;
};

static struct rf_node rf_nodes[RF_NODE_MAX];
static u32 rf_nnode;

static u32 rf_hex8(const char *p)
{
    u32 v = 0, i;

    for (i = 0; i < 8; i++) {
        char c = p[i];
        u32 d;

        if (c >= '0' && c <= '9')
            d = (u32)(c - '0');
        else if (c >= 'A' && c <= 'F')
            d = (u32)(c - 'A' + 10);
        else if (c >= 'a' && c <= 'f')
            d = (u32)(c - 'a' + 10);
        else
            return 0;                   /* 野域按 0:走查自然停卷 */
        v = (v << 4) | d;
    }
    return v;
}

static u32 rf_align4(u32 x)
{
    return (x + 3u) & ~3u;
}

static void rf_parse(void)
{
    const char *base = rt_ramfs_start;
    u32 total = (u32)(rt_ramfs_end - rt_ramfs_start);
    u32 pos = 0;

    while (pos + 110 <= total && rf_nnode < RF_NODE_MAX) {
        const char *p = base + pos;
        u32 filesize, namesize, dataoff;
        const char *name;
        struct rf_node *n;
        u32 k;

        if (p[0] != '0' || p[1] != '7' || p[2] != '0' ||
            p[3] != '7' || p[4] != '0' || p[5] != '1')
            break;                      /* 魔数不继:卷止(诚实停) */
        filesize = rf_hex8(p + 54);     /* 域 6 filesize@54 */
        namesize = rf_hex8(p + 94);    /* 域 11 namesize@94 */
        if (namesize == 0 || namesize > 60)
            break;                      /* 野条目:停卷 */
        name = p + 110;
        if (pos + 110 + namesize > total)
            break;
        if (name[0] == 'T' && name[1] == 'R' && name[2] == 'A' &&
            name[3] == 'I' && name[4] == 'L' && name[5] == 'E' &&
            name[6] == 'R' && filesize == 0)
            break;                      /* TRAILER!!!:收卷 */
        dataoff = rf_align4(pos + 110 + namesize);
        if (dataoff + filesize > total)
            break;
        /* 挂名形路径须含前导 '/' 且收进节点窗:超长条目跳过
         * (不注册=查名不中,诚实的不可服务面)。 */
        if (1u + (namesize - 1u) + 1u > sizeof(rf_nodes[0].path)) {
            pos = rf_align4(dataoff + filesize);
            continue;
        }
        n = &rf_nodes[rf_nnode++];
        n->path[0] = '/';
        for (k = 0; k + 1 < namesize; k++)
            n->path[1 + k] = name[k];
        n->path[namesize] = '\0';
        n->off = dataoff;
        n->size = filesize;
        pos = rf_align4(dataoff + filesize);
    }
}

static int rf_find(const char *path)
{
    u32 i, j;

    for (i = 0; i < rf_nnode; i++) {
        const char *b = rf_nodes[i].path;

        for (j = 0; j < RT_FD_DATA; j++) {
            if (path[j] != b[j])
                break;
            if (path[j] == '\0')
                return (int)i;
        }
    }
    return -2;                          /* -ENOENT */
}

static void srv_ramfs(void *arg)
{
    struct rt_fdreq req;
    struct rt_recvinfo ri;
    int chid;
    long r;
    u32 i;

    (void)arg;
    chid = rt_msg_ch_create(0);
    if (chid <= 0)
        return;
    rf_parse();
    for (i = 0; i < rf_nnode; i++)
        rt_msg_name_attach(rf_nodes[i].path, chid);
    for (;;) {
        if (rt_msg_recv(chid, &req, sizeof(req), RT_WAIT_FOREVER, &ri) != 0)
            continue;
        if (ri.rcvid == 0)
            continue;
        r = -22;
        switch (req.op) {
        case RT_FDOP_OPEN: {
            int idx = rf_find(req.data);

            r = idx < 0 ? -2 : (long)ocb_alloc((u32)idx);
            break;
        }
        case RT_FDOP_READ: {
            struct srv_ocb *o = ocb_get((int)req.ocb);
            const struct rf_node *nd;
            const char *src;
            u32 n;

            if (!o || o->node >= rf_nnode) {
                rt_msg_reply(chid, ri.rcvid, NULL, 0, -9);
                continue;
            }
            nd = &rf_nodes[o->node];
            n = req.a > RT_FD_RDATA ? RT_FD_RDATA : req.a;
            if (o->off >= nd->size)
                n = 0;                  /* EOF:status=0 面即此 */
            else if (n > nd->size - o->off)
                n = nd->size - o->off;
            src = rt_ramfs_start + nd->off + o->off;
            o->off += n;                /* 文件位置在 ocb 推进(resmgr 本义) */
            rt_msg_reply(chid, ri.rcvid, src, n, (long)n);
            continue;                   /* READ 走 rbuf 面:自答自续 */
        }
        case RT_FDOP_CLOSE:
            r = ocb_release((int)req.ocb);
            break;
        case RT_FDOP_DUP:
            r = ocb_ref((int)req.ocb);
            break;
        case RT_FDOP_SEEK: {          /* 站33:SEEK_SET 绝对位 */
            struct srv_ocb *o = ocb_get((int)req.ocb);

            if (!o) {
                r = -9;
                break;
            }
            o->off = req.a;
            r = 0;
            break;
        }
        default:
            r = -22;                   /* WRITE/未知:文件面无此操作 */
        }
        rt_msg_reply(chid, ri.rcvid, NULL, 0, r);
    }
}

/* ---- PM(一次性):stdio 0/1/2 绑 /dev/tty 后退场 ----
 * 参考实现 PM 固定序列形状的站内残部:真正的"PM 拉服务序列"=
 * 协商位留后(F3-18);本任务只做 stdio 契约面。 */

static void srv_pm(void *arg)
{
    rt_task_t *shell = arg;
    struct rt_proc *p = shell ? shell->proc : NULL;
    int i;

    if (!p)
        return;
    for (i = 0; i < 3; i++) {           /* stdio 0/1/2 三元组 */
        int tries = 0;
        int fd = -2;

        while (tries++ < 20) {
            fd = rt_fd_open(p, "/dev/tty");
            if (fd >= 0)
                break;
            rt_sleep_ticks(1);         /* 挂名未就:下一拍再试 */
        }
        if (fd != i) {
            rt_puts("srv: pm stdio bind failed\n");
            return;
        }
    }
    rt_puts_u64("srv: pm bound stdio, ramfs files=", (u64)rf_nnode, "\n");
}

/* ---- fd 层(内核语境客户端;调用者=EL0 syscall 包装或 PM) ---- */

static int fd_slot_ok(struct rt_proc *p, int fd)
{
    return p && fd >= 0 && (u32)fd < RT_FD_MAX && p->fds[fd].used;
}

int rt_fd_open(struct rt_proc *p, const char *kpath)
{
    struct rt_fdreq req;
    int coid, fd, r;
    u32 i;

    if (!p)
        return -22;
    coid = rt_msg_name_open(kpath);
    if (coid < 0)
        return coid;                   /* -ENOENT:无人挂此名 */
    fd_zero(&req, sizeof(req));
    req.op = RT_FDOP_OPEN;
    for (i = 0; i + 1 < RT_FD_DATA && kpath[i]; i++)
        req.data[i] = kpath[i];
    req.data[i] = '\0';
    r = (int)rt_msg_send(coid, &req, sizeof(req), NULL, 0, SRV_TICKS);
    if (r < 0) {
        rt_msg_conn_detach(coid);
        return r;
    }
    for (fd = 0; fd < (int)RT_FD_MAX; fd++)
        if (!p->fds[fd].used)
            break;
    if (fd == (int)RT_FD_MAX) {        /* 槽满:礼貌回滚 ocb 再报 */
        req.op = RT_FDOP_CLOSE;
        req.ocb = (u32)r;
        rt_msg_send(coid, &req, sizeof(req), NULL, 0, CLOSE_TICKS);
        rt_msg_conn_detach(coid);
        return -12;
    }
    p->fds[fd].used = 1;
    p->fds[fd].coid = coid;
    p->fds[fd].ocb = (u32)r;           /* status=ocb 号(服务器立) */
    return fd;
}

long rt_fd_read(struct rt_proc *p, int fd, char *kbuf, u32 len)
{
    struct rt_fdreq req;
    long r;

    if (!fd_slot_ok(p, fd))
        return -9;
    if (len > RT_FD_RDATA)
        len = RT_FD_RDATA;
    fd_zero(&req, sizeof(req));
    req.op = RT_FDOP_READ;
    req.a = len;
    req.ocb = p->fds[fd].ocb;
    /* 回复数据直落 kbuf(rbuf 面);status 即返回值:0=EOF,>0=n */
    r = rt_msg_send(p->fds[fd].coid, &req, sizeof(req), kbuf, len,
                    SRV_TICKS);
    if (r < 0)
        return r;
    return r > (long)len ? (long)len : r;
}

long rt_fd_write(struct rt_proc *p, int fd, const char *kbuf, u32 len)
{
    struct rt_fdreq req;
    long r;
    u32 i;

    if (!fd_slot_ok(p, fd))
        return -9;
    if (len > RT_FD_DATA)
        len = RT_FD_DATA;
    fd_zero(&req, sizeof(req));
    req.op = RT_FDOP_WRITE;
    req.a = len;
    req.ocb = p->fds[fd].ocb;     /* 站37 定谳:站31 起此处漏填
                                   * ——tty/ramfs 的 WRITE 不读 ocb
                                   * 故无感;pipesrv 按端分发即爆
                                   * (PIPEDBG: write ocb=0 实抓) */
    for (i = 0; i < len; i++)
        req.data[i] = kbuf[i];
    r = rt_msg_send(p->fds[fd].coid, &req, sizeof(req), NULL, 0,
                    SRV_TICKS);
    return r < 0 ? r : (r > (long)len ? (long)len : r);
}

/* 站33:SEEK_SET 绝对位(loader 段定位;whence 其余面=调用侧拒) */
long rt_fd_seek(struct rt_proc *p, int fd, u32 off)
{
    struct rt_fdreq req;
    long r;

    if (!fd_slot_ok(p, fd))
        return -9;
    fd_zero(&req, sizeof(req));
    req.op = RT_FDOP_SEEK;
    req.a = off;
    req.ocb = p->fds[fd].ocb;
    r = rt_msg_send(p->fds[fd].coid, &req, sizeof(req), NULL, 0,
                    SRV_TICKS);
    return r < 0 ? r : 0;
}

int rt_fd_close(struct rt_proc *p, int fd)
{
    struct rt_fdreq req;
    struct rt_fd *f;
    int r;

    if (!fd_slot_ok(p, fd))
        return -9;
    f = &p->fds[fd];
    fd_zero(&req, sizeof(req));
    req.op = RT_FDOP_CLOSE;
    req.ocb = f->ocb;
    r = (int)rt_msg_send(f->coid, &req, sizeof(req), NULL, 0,
                         CLOSE_TICKS);
    rt_msg_conn_detach(f->coid);       /* 引用减一(0 即自由) */
    f->used = 0;
    return r < 0 ? r : 0;
}

/* fork 组拷(参考实现 COPY_UCONTEXT 同款):fdtable 逐槽复制+两侧
 * 引用各+1——客户端 coid(rt_msg_conn_ref)与服务器 ocb(DUP 消息)
 * 互为镜像;任一失败即不继承该槽(不悬空)。 */
static int fd_dup_server(const struct rt_fd *f)
{
    struct rt_fdreq req;

    fd_zero(&req, sizeof(req));
    req.op = RT_FDOP_DUP;
    req.ocb = f->ocb;
    return (int)rt_msg_send(f->coid, &req, sizeof(req), NULL, 0,
                            CLOSE_TICKS);
}

void rt_fd_fork(struct rt_proc *pp, struct rt_proc *cp)
{
    u32 i;

    for (i = 0; i < RT_FD_MAX; i++) {
        if (!pp->fds[i].used)
            continue;
        if (rt_msg_conn_ref(pp->fds[i].coid) == 0 &&
            fd_dup_server(&pp->fds[i]) == 0)
            cp->fds[i] = pp->fds[i];
    }
}

void rt_fd_close_all(struct rt_proc *p)
{
    u32 i;

    if (!p)
        return;
    for (i = 0; i < RT_FD_MAX; i++) {
        if (p->fds[i].used)
            rt_fd_close(p, (int)i);    /* CLOSE->ocb 引用减+断连 */
    }
}

/* ---- 开机链(固定序列;sys_exit 的 fd 关闭在 rt_syscall.c) ---- */

void rt_srv_boot(rt_task_t *shell)
{
    if (!rt_task_create("ttysrv", srv_tty, NULL, 8))
        rt_puts("srv: ttysrv spawn failed\n");
    if (!rt_task_create("ramfs", srv_ramfs, NULL, 8))
        rt_puts("srv: ramfs spawn failed\n");
    if (!rt_task_create("pm", srv_pm, shell, 8))
        rt_puts("srv: pm spawn failed\n");
    rt_pipe_boot();                  /* 站37:pipesrv(stdio 绑定后起,
                                      * 与 tty/ramfs 无交互序) */
}
