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
        case RT_FDOP_STAT:            /* 站40:mode<<32|size(字符面) */
            r = ((long)0x2190u) << 32;
            break;
        case RT_FDOP_POLL:            /* 站43:可读=串口 RX 环非空 */
            r = rt_rx_pending() ? 1 : 0;
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
    u32 size;                           /* 当前长(cow 追加随之长) */
    u32 cow;                            /* 站39:写时复制页 PA(0=rodata
                                         * 后备);死节点=path[0]==0 */
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
        n->cow = 0;
        pos = rf_align4(dataoff + filesize);
    }
}

static int rf_find(const char *path)
{
    u32 i, j;

    for (i = 0; i < rf_nnode; i++) {
        const char *b = rf_nodes[i].path;

        if (!b[0])
            continue;                   /* 站39:死节点(已 unlink) */
        for (j = 0; j < RT_FD_DATA; j++) {
            if (path[j] != b[j])
                break;
            if (path[j] == '\0')
                return (int)i;
        }
    }
    return -2;                          /* -ENOENT */
}

/* ---- 站四十:目录前缀柄(openat("/etc") 面) ----
 * 参考实现律:名空间归服务器——目录不是挂名,是 ramfs 服务器对
 * "前缀匹配"的解析结果(直名未挂者回退路由到元名,解析在此)。
 * 柄编码=0x100|idx,与文件 ocb 1..N 区隔(READ 拒 EISDIR,
 * READDIR 以前缀过滤+basename 出+首现去重)。 */
#define RF_DIR_TAG  0x100u
#define RF_DIR_MAX  4u
struct rf_dir {
    u32 used;
    char prefix[RT_FD_DATA];
};
static struct rf_dir rf_dirs[RF_DIR_MAX];

/* b 是否 pre[0..dl) 前缀下的名字(b[dl]=='/' 或 NUL) */
static int rf_under(const char *b, const char *pre, u32 dl)
{
    u32 j;

    for (j = 0; j < dl; j++)
        if (b[j] != pre[j])
            return 0;
    return b[dl] == '/' || b[dl] == '\0';
}

/* pre 下是否还有活名(目录存在性) */
static int rf_prefix_any(const char *pre, u32 dl)
{
    u32 i;

    for (i = 0; i < rf_nnode; i++)
        if (rf_nodes[i].path[0] && rf_under(rf_nodes[i].path, pre, dl))
            return 1;
    return 0;
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
    /* 站39:目录枚举元名(OPEN 之返 ocb=0=目录柄;READDIR 专用) */
    rt_msg_name_attach("/dev/ramfs", chid);
    for (;;) {
        if (rt_msg_recv(chid, &req, sizeof(req), RT_WAIT_FOREVER, &ri) != 0)
            continue;
        if (ri.rcvid == 0)
            continue;
        r = -22;
        switch (req.op) {
        case RT_FDOP_OPEN: {
            int idx = rf_find(req.data);

            if (idx >= 0) {
                r = (long)ocb_alloc((u32)idx);
                break;
            }
            if (req.data[0] == '/' && req.data[1] == 'd' &&
                req.data[2] == 'e' && req.data[3] == 'v' &&
                req.data[4] == '/' && req.data[5] == 'r' &&
                req.data[6] == 'a' && req.data[7] == 'm' &&
                req.data[8] == 'f' && req.data[9] == 's' &&
                !req.data[10]) {
                r = 0;                   /* 元名:全局枚举柄(站39) */
                break;
            }
            {   /* 站40:目录前缀柄——名空间归服务器,解析在此 */
                u32 dl = 0, k2;

                while (req.data[dl] && dl < RT_FD_DATA)
                    dl++;
                if (dl && rf_prefix_any(req.data, dl)) {
                    for (k2 = 0; k2 < RF_DIR_MAX; k2++)
                        if (!rf_dirs[k2].used) {
                            u32 m;

                            rf_dirs[k2].used = 1;
                            for (m = 0; m + 1 < RT_FD_DATA && req.data[m]; m++)
                                rf_dirs[k2].prefix[m] = req.data[m];
                            rf_dirs[k2].prefix[m] = '\0';
                            r = (long)(RF_DIR_TAG | (k2 + 1));
                            break;
                        }
                    if (k2 == RF_DIR_MAX)
                        r = -12;
                    break;
                }
            }
            r = -2;
            break;
        }
        case RT_FDOP_READ: {
            struct srv_ocb *o = req.ocb >= RF_DIR_TAG ? NULL :
                ocb_get((int)req.ocb);
            struct rf_node *nd;
            const char *src;
            u32 n;

            if (!o || o->node >= rf_nnode) {
                rt_msg_reply(chid, ri.rcvid, NULL, 0,
                             req.ocb >= RF_DIR_TAG ? -21 : -9);
                continue;                   /* 目录柄=EISDIR(站40) */
            }
            nd = &rf_nodes[o->node];
            n = req.a > RT_FD_RDATA ? RT_FD_RDATA : req.a;
            if (o->off >= nd->size)
                n = 0;                  /* EOF:status=0 面即此 */
            else if (n > nd->size - o->off)
                n = nd->size - o->off;
            src = nd->cow ? (const char *)(unsigned long)nd->cow + o->off
                          : rt_ramfs_start + nd->off + o->off;
            o->off += n;                /* 文件位置在 ocb 推进(resmgr 本义) */
            rt_msg_reply(chid, ri.rcvid, src, n, (long)n);
            continue;                   /* READ 走 rbuf 面:自答自续 */
        }
        case RT_FDOP_WRITE: {         /* 站39:写时复制到页池页(≤4KB) */
            struct srv_ocb *o = ocb_get((int)req.ocb);
            struct rf_node *nd;
            u32 n = req.a > RT_FD_DATA ? RT_FD_DATA : req.a;
            u32 i2;

            if (!o || o->node >= rf_nnode) {
                r = -9;
                break;
            }
            nd = &rf_nodes[o->node];
            if (o->off + n > 4096u) {
                r = -12;               /* 4KB/文件 v0 上限(一页) */
                break;
            }
            if (!nd->cow) {            /* 首写:内容拷入页池页 */
                u64 pg = rt_as_page_alloc();

                if (!pg) {
                    r = -12;
                    break;
                }
                nd->cow = (u32)pg;
                for (i2 = 0; i2 < nd->size && i2 < 4096u; i2++)
                    ((char *)(unsigned long)pg)[i2] =
                        rt_ramfs_start[nd->off + i2];
                for (; i2 < 4096u; i2++)
                    ((char *)(unsigned long)pg)[i2] = 0;
            }
            for (i2 = 0; i2 < n; i2++)
                ((char *)(unsigned long)nd->cow)[o->off + i2] =
                    req.data[i2];
            o->off += n;
            if (o->off > nd->size)
                nd->size = o->off;     /* 追加:长随位置长 */
            r = (long)n;
            break;
        }
        case RT_FDOP_CLOSE:
            if (!req.ocb)
                r = 0;                 /* 目录柄(0)关=无簿记 */
            else if (req.ocb >= RF_DIR_TAG) {
                u32 d = req.ocb & 0xFFu;

                if (d && d <= RF_DIR_MAX && rf_dirs[d - 1].used)
                    rf_dirs[d - 1].used = 0;
                r = 0;
            } else
                r = ocb_release((int)req.ocb);
            break;
        case RT_FDOP_POLL: {          /* 站43:可读=文件位置未到 EOF
                                      * (目录柄=枚举未尽=恒可读) */
            struct srv_ocb *o = ocb_get((int)req.ocb);
            struct rf_node *nd;

            if (!o || o->node >= rf_nnode) {
                r = req.ocb >= RF_DIR_TAG ? 1 : -9;
                break;
            }
            nd = &rf_nodes[o->node];
            r = o->off < nd->size ? 1 : 0;
            break;
        }
        case RT_FDOP_DUP:
            if (!req.ocb || req.ocb >= RF_DIR_TAG) {
                r = 0;                 /* 目录柄 DUP=零簿记(v0 记档) */
                break;
            }
            r = ocb_ref((int)req.ocb);
            break;
        case RT_FDOP_SEEK: {          /* 站39:rsv=whence(0SET/1CUR/2END) */
            struct srv_ocb *o = ocb_get((int)req.ocb);

            if (!o) {
                r = -9;
                break;
            }
            if (req.rsv == 1u)
                o->off += req.a;
            else if (req.rsv == 2u)
                o->off = rf_nodes[o->node].size + req.a;
            else
                o->off = req.a;        /* 站33 契约:默认 SET */
            if (o->off > rf_nodes[o->node].size)
                o->off = rf_nodes[o->node].size;   /* 越尾夹回(END 后读=EOF) */
            r = (long)o->off;
            break;
        }
        case RT_FDOP_CREATE: {        /* 站39:建即开(新空文件+挂名) */
            int idx = rf_find(req.data);

            if (idx >= 0) {
                r = -17;               /* -EEXIST */
                break;
            }
            if (rf_nnode >= RF_NODE_MAX) {
                r = -12;
                break;
            }
            idx = (int)rf_nnode++;
            {
                struct rf_node *nd = &rf_nodes[idx];
                u32 k;

                for (k = 0; k + 1 < RT_FD_DATA && req.data[k]; k++)
                    nd->path[k] = req.data[k];
                nd->path[k] = '\0';
                nd->off = 0;
                nd->size = 0;
                nd->cow = 0;
            }
            rt_msg_name_attach(rf_nodes[idx].path, chid);
            r = (long)ocb_alloc((u32)idx);
            break;
        }
        case RT_FDOP_UNLINK: {        /* 站39:死节点标记(免索引挪移
                                      * 撕开在册 ocb;页还池) */
            int idx = rf_find(req.data);

            if (idx < 0) {
                r = -2;
                break;
            }
            if (rf_nodes[idx].cow)
                rt_as_page_free(rf_nodes[idx].cow);
            rf_nodes[idx].cow = 0;
            rf_nodes[idx].path[0] = '\0';
            rf_nodes[idx].size = 0;
            rt_msg_name_detach(req.data);
            r = 0;
            break;
        }
        case RT_FDOP_READDIR: {       /* 站39 单答形(定谳:matched
                                      * continue 只续 for 环——连环
                                      * reply 终末覆写 status,恒收 0;
                                      * 单答形:一次走查恰好一次回执)。
                                      * 站40:回执=[d_type][名];两脸——
                                      * ocb=0 全局枚举(全路径,lsdemo 面),
                                      * ocb=目录前缀柄=basename 枚举
                                      * (首现去重 O(n²) 小表;名下有深径
                                      * =DT_DIR/busybox getdents64 面)。 */
            u32 cur = req.a, live = 0, k = 0;
            int answered = 0;

            if (req.ocb >= RF_DIR_TAG) {    /* 目录前缀柄脸 */
                u32 d = req.ocb & 0xFFu;
                struct rf_dir *dir = d && d <= RF_DIR_MAX &&
                    rf_dirs[d - 1].used ? &rf_dirs[d - 1] : NULL;
                u32 dl = 0;

                if (!dir) {
                    rt_msg_reply(chid, ri.rcvid, NULL, 0, -9);
                    continue;
                }
                while (dir->prefix[dl] && dl < RT_FD_DATA)
                    dl++;
                for (i = 0; i < rf_nnode && !answered; i++) {
                    const char *b = rf_nodes[i].path;
                    const char *c;
                    u32 cl = 0, first = 1, i2;

                    if (!b[0] || !rf_under(b, dir->prefix, dl) || b[dl] != '/')
                        continue;
                    c = b + dl + 1;
                    while (c[cl] && c[cl] != '/')
                        cl++;
                    for (i2 = 0; i2 < i; i2++) {   /* 首现去重 */
                        const char *o = rf_nodes[i2].path;
                        const char *c2;
                        u32 ol = 0, m;

                        if (!o[0] || !rf_under(o, dir->prefix, dl) ||
                            o[dl] != '/')
                            continue;
                        c2 = o + dl + 1;
                        while (c2[ol] && c2[ol] != '/')
                            ol++;
                        if (ol != cl)
                            continue;
                        for (m = 0; m < cl && c2[m] == c[m]; m++)
                            ;
                        if (m == cl) {
                            first = 0;
                            break;
                        }
                    }
                    if (!first)
                        continue;
                    if (live == cur) {
                        u8 dt = 8u;      /* DT_REG;有深径=DT_DIR */
                        u32 i3;

                        for (i3 = 0; i3 < rf_nnode; i3++) {
                            const char *o = rf_nodes[i3].path;

                            if (!o[0])
                                continue;
                            if (rf_under(o, b, dl + 1 + cl) &&
                                o[dl + 1 + cl] == '/') {
                                dt = 4u;
                                break;
                            }
                        }
                        req.data[0] = (char)dt;
                        for (k = 1; k - 1 < cl && k + 1 < RT_FD_RDATA; k++)
                            req.data[k] = c[k - 1];
                        req.data[k] = '\0';
                        k++;
                        answered = 1;
                    } else {
                        live++;
                    }
                }
            } else {                    /* 全局脸(站39 lsdemo 契约) */
                for (i = 0; i < rf_nnode && !answered; i++) {
                    if (!rf_nodes[i].path[0])
                        continue;
                    if (live == cur) {
                        req.data[0] = (char)8u;   /* DT_REG(全路径面) */
                        for (k = 1; k + 1 < RT_FD_RDATA &&
                                    rf_nodes[i].path[k - 1]; k++)
                            req.data[k] = rf_nodes[i].path[k - 1];
                        req.data[k] = '\0';
                        k++;
                        answered = 1;
                    } else {
                        live++;
                    }
                }
            }
            if (answered)
                rt_msg_reply(chid, ri.rcvid, req.data, k, (long)(live + 1));
            else
                rt_msg_reply(chid, ri.rcvid, NULL, 0, 0);
            continue;                  /* 已答自续:不落公共回执 */
        }
        case RT_FDOP_STAT: {          /* 站40:mode<<32|size(newfstatat) */
            int idx = rf_find(req.data);

            if (idx >= 0) {
                r = ((long)0x81A4u << 32) | rf_nodes[idx].size;
                break;                   /* S_IFREG|0644 */
            }
            {
                u32 dl = 0;

                while (req.data[dl] && dl < RT_FD_DATA)
                    dl++;
                if (dl && rf_prefix_any(req.data, dl))
                    r = (long)0x41EDu << 32;   /* S_IFDIR|0755 */
                else
                    r = -2;
            }
            break;
        }
        default:
            r = -22;
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

/* ---- 站41:CAPR fail-closed 执法点(fd 六门) ----
 * 无 CAP_VFS 即拒(-EPERM);零能力 create 起步的进程连名字
 * 都摸不到。fail-closed=缺省拒绝,授权是显式且唯一的。 */
static int vfs_check(struct rt_proc *p)
{
    return p && (p->caps & CAP_VFS);
}

/* rlimit:fd 槽满额判定(自降单向,默认 RT_FD_MAX) */
static u32 fd_used_count(struct rt_proc *p)
{
    u32 i, n = 0;

    for (i = 0; i < RT_FD_MAX; i++)
        if (p->fds[i].used)
            n++;
    return n;
}

int rt_fd_open(struct rt_proc *p, const char *kpath)
{
    struct rt_fdreq req;
    int coid, fd, r;
    u32 i;

    if (!p)
        return -22;
    if (!vfs_check(p))
        return -1;                     /* 站41:CAP_VFS fail-closed */
    if (fd_used_count(p) >= p->max_fd)
        return -24;                    /* 站41:rlimit EMFILE */
    coid = rt_msg_name_open(kpath);
    if (coid < 0) {
        /* 站40:目录回退——直名未挂者路由 ramfs 元名,由服务器解析
         * (文件/目录前缀/-ENOENT;openat("/etc") 即走此路)。 */
        coid = rt_msg_name_open("/dev/ramfs");
        if (coid < 0)
            return coid;
    }
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
static long fd_seek_impl(struct rt_proc *p, int fd, u64 off, u32 whence)
{
    struct rt_fdreq req;
    long r;

    if (!fd_slot_ok(p, fd))
        return -9;
    fd_zero(&req, sizeof(req));
    req.op = RT_FDOP_SEEK;
    req.a = (u32)off;
    req.rsv = whence;                   /* 站39:whence 走 rsv 槽 */
    req.ocb = p->fds[fd].ocb;
    r = rt_msg_send(p->fds[fd].coid, &req, sizeof(req), NULL, 0,
                    SRV_TICKS);
    return r < 0 ? r : r;
}

long rt_fd_seek(struct rt_proc *p, int fd, u32 off)
{
    return fd_seek_impl(p, fd, off, 0u);      /* 站33 契约面:默认 SET */
}

int rt_fd_seek_whence(struct rt_proc *p, int fd, u64 off, u32 whence)
{
    return (int)fd_seek_impl(p, fd, off, whence);  /* 服务器返新位/负 errno */
}

/* 站39:建即开(create 与 open 同径:回执即 ocb,fd 登记同式) */
long rt_fd_create(struct rt_proc *p, const char *kpath)
{
    struct rt_fdreq req;
    int coid, fd, r;
    u32 i;

    if (!p)
        return -22;
    if (!vfs_check(p))
        return -1;                     /* 站41:CAP_VFS fail-closed */
    if (fd_used_count(p) >= p->max_fd)
        return -24;                    /* 站41:rlimit EMFILE */
    /* 新文件尚无挂名:CREATE 走 ramfs 元名路由(服务器建名即挂名,
     * 后续 open 按名寻径);fd 三元组的 coid 亦驻此连接(生命周期
     * 同 open 面:close 时 CLOSE 消息+conn_detach)。 */
    coid = rt_msg_name_open("/dev/ramfs");
    if (coid < 0)
        return coid;
    fd_zero(&req, sizeof(req));
    req.op = RT_FDOP_CREATE;
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
    if (fd == (int)RT_FD_MAX) {
        req.op = RT_FDOP_CLOSE;
        req.ocb = (u32)r;
        rt_msg_send(coid, &req, sizeof(req), NULL, 0, CLOSE_TICKS);
        rt_msg_conn_detach(coid);
        return -12;
    }
    p->fds[fd].used = 1;
    p->fds[fd].coid = coid;
    p->fds[fd].ocb = (u32)r;
    return fd;
}

long rt_fd_unlink(struct rt_proc *p, const char *kpath)
{
    struct rt_fdreq req;
    int coid, r;
    u32 i;

    if (!p)
        return -22;
    if (!vfs_check(p))
        return -1;                     /* 站41:CAP_VFS fail-closed */
    coid = rt_msg_name_open(kpath);
    if (coid < 0)
        return coid;                   /* -ENOENT:无人挂此名 */
    fd_zero(&req, sizeof(req));
    req.op = RT_FDOP_UNLINK;
    for (i = 0; i + 1 < RT_FD_DATA && kpath[i]; i++)
        req.data[i] = kpath[i];
    req.data[i] = '\0';
    r = (int)rt_msg_send(coid, &req, sizeof(req), NULL, 0, CLOSE_TICKS);
    rt_msg_conn_detach(coid);          /* 摘名后此连接即弃 */
    return r;
}

/* 目录枚举:游标进,路径出,返下一游标(0=末) */
long rt_fd_dir(struct rt_proc *p, int index, char *kpath, u32 len)
{
    struct rt_fdreq req;
    struct rt_recvinfo ri;
    long r;
    int coid;

    if (!p || !kpath || !len)
        return -22;
    if (!vfs_check(p))
        return -1;                     /* 站41:CAP_VFS fail-closed */
    coid = rt_msg_name_open("/dev/ramfs");
    if (coid < 0)
        return coid;
    fd_zero(&req, sizeof(req));
    req.op = RT_FDOP_READDIR;
    req.a = (u32)index;
    r = rt_msg_send(coid, &req, sizeof(req), kpath, len, SRV_TICKS);
    rt_msg_conn_detach(coid);
    if (r <= 0)
        return r;                       /* 0=枚举终;负=errno */
    (void)ri;
    {   /* 站40:回执=[d_type][名]——剥型字节,名左移一 */
        u32 j;

        for (j = 1; j < len && kpath[j]; j++)
            kpath[j - 1] = kpath[j];
        kpath[j - 1] = '\0';
    }
    return r;                           /* 正=下一游标(status 面;名
                                          * 已落 kpath。每枚举一事一
                                          * 连接往返=v0 诚实成本记档) */
}

/* 站43:可读查询往返(POLL wire op 的客户端面;ppoll 每有效 fd
 * 一次串行往返——轮询面 v0,事件回调=后弧)。返 1 可读/0 无/负错。 */
long rt_fd_poll(struct rt_proc *p, int fd)
{
    struct rt_fdreq req;
    struct rt_fd *f;

    if (!fd_slot_ok(p, fd))
        return -9;
    f = &p->fds[fd];
    fd_zero(&req, sizeof(req));
    req.op = RT_FDOP_POLL;
    req.ocb = f->ocb;
    return rt_msg_send(f->coid, &req, sizeof(req), NULL, 0, SRV_TICKS);
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

/* ---- 站40:busybox POSIX 薄面(fd 层三门) ---- */

/* getdents64:一次一名(POSIX 合法面,短读短答)。fd 私域 dircur
 * 持游标,回执 status 兑付下一游标(0=末)。dirent64 手搓布局
 * (aarch64:ino/off 各 8B+reclen 2B+type 1B+名)。 */
long rt_fd_getdents64(struct rt_proc *p, int fd, char *ubuf, u32 len)
{
    struct rt_fdreq req;
    char kname[RT_FD_RDATA];
    struct rt_fd *f;
    long r;
    u32 nl = 0, reclen, i;
    struct {
        u64 ino;
        long off;
        u16 reclen;
        u8 type;
        char name[RT_FD_RDATA];
    } de;

    if (!fd_slot_ok(p, fd))
        return -9;
    if (!vfs_check(p))
        return -1;                     /* 站41:CAP_VFS fail-closed */
    f = &p->fds[fd];
    fd_zero(&req, sizeof(req));
    req.op = RT_FDOP_READDIR;
    req.ocb = f->ocb;
    req.a = f->dircur;
    r = rt_msg_send(f->coid, &req, sizeof(req), kname, sizeof(kname),
                    SRV_TICKS);
    if (r < 0)
        return r;
    if (r == 0)
        return 0;                      /* EOF:游标不动(幂等重读) */
    for (i = 1; i < sizeof(kname) && kname[i]; i++)
        nl++;                          /* 名长(NUL 不计) */
    reclen = (19u + nl + 1u + 7u) & ~7u;
    if (reclen > len)
        return -22;                    /* 单项都容不下(POSIX:EINVAL) */
    for (i = 0; i < sizeof(de); i++)
        ((char *)&de)[i] = 0;      /* 零填:name 收 NUL、尾垫干净(busybox 消费面) */
    de.ino = f->dircur + 1;
    de.off = r;
    de.reclen = (u16)reclen;
    de.type = (u8)kname[0];
    for (i = 0; i < nl; i++)
        de.name[i] = kname[1 + i];
    for (i = 0; i < reclen; i++)
        ubuf[i] = ((const char *)&de)[i];   /* 存 UB:syscall 层已 access_ok */
    f->dircur = (u32)r;                /* 兑付下一游标 */
    return (long)reclen;
}

/* newfstatat:mode<<32|size(status 面;busybox ls 分类面) */
long rt_fd_fstatat(struct rt_proc *p, const char *kpath, u32 *mode,
                   u32 *size)
{
    struct rt_fdreq req;
    int coid;
    long r;
    u32 i;

    if (!p || !kpath)
        return -22;
    if (!vfs_check(p))
        return -1;                     /* 站41:CAP_VFS fail-closed */
    coid = rt_msg_name_open(kpath);
    if (coid < 0) {
        coid = rt_msg_name_open("/dev/ramfs");   /* 目录回退(OPEN 同律) */
        if (coid < 0)
            return coid;
    }
    fd_zero(&req, sizeof(req));
    req.op = RT_FDOP_STAT;
    for (i = 0; i + 1 < RT_FD_DATA && kpath[i]; i++)
        req.data[i] = kpath[i];
    req.data[i] = '\0';
    r = rt_msg_send(coid, &req, sizeof(req), NULL, 0, SRV_TICKS);
    rt_msg_conn_detach(coid);
    if (r < 0)
        return r;
    if (mode)
        *mode = (u32)((u64)r >> 32);
    if (size)
        *size = (u32)r;
    return 0;
}

/* dup3:DUP 消息+conn 引用(=fork 单槽同款双侧镜像) */
long rt_fd_dup3(struct rt_proc *p, int oldfd, int newfd)
{
    struct rt_fdreq req;
    struct rt_fd *f;

    if (!fd_slot_ok(p, oldfd))
        return -9;
    if (newfd < 0 || (u32)newfd >= RT_FD_MAX)
        return -9;
    if (newfd == oldfd)
        return newfd;
    if (p->fds[newfd].used)
        rt_fd_close(p, newfd);         /* POSIX:隐式关闭先 */
    f = &p->fds[oldfd];
    if (rt_msg_conn_ref(f->coid) != 0)
        return -12;
    fd_zero(&req, sizeof(req));
    req.op = RT_FDOP_DUP;
    req.ocb = f->ocb;
    if (rt_msg_send(f->coid, &req, sizeof(req), NULL, 0, CLOSE_TICKS) < 0) {
        rt_msg_conn_detach(f->coid);
        return -12;
    }
    p->fds[newfd] = *f;
    return newfd;
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
    rt_blk_srv_boot();               /* 站39:blksrv(IRQ 登记+第四服
                                      * 务器;探针相已在 boot 序前段) */
    rt_rng_boot();                   /* 站40:rngsrv(第五,/dev/random) */
    rt_procfs_boot();                /* 站40:procsrv(第六,/proc) */
    rt_slog_boot();                  /* 站40:slogsrv(第七,/dev/slog) */
}
