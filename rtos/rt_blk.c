/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "rtos.h"
#include "miniuart.h"    /* 站50:puts_uart 声明前置(gic.h 静态
                           * gic_dist_init 消费;头单含契约) */
#include "gic.h"         /* GUEST_INTID_VIRTIO(单含静态头) */
#include "virtio.h"

/* 站三十九:blkdev——第四个 resmgr 服务器 + 参数化 virtio-blk 面。
 * 参考实现机制白描:VFS=用户态 server 统一层;blk=真盘裸面(F5 块
 * 设备=裸盘无分区,v0)。**数据后端=宿主 vblk 设备模型**(站 9-10
 * 的静态盘:desc/avail/used 全链由宿主 EMULATED 面服务,免
 * QEMU -drive)——"文件内容只经消息送达"红线不破:扇区缓冲驻
 * 服务器静态区(=IPA 真值),用户窗永不映射盘页。
 *
 * 语义:ocb.off=字节位;READ=按位读窗(先整扇入缓冲再切窗),
 * WRITE=读改写(扇区入缓冲,窗内补 req.a 字节,整扇写回);
 * off 跨扇界=诚实拒绝(-EINVAL,单窗单扇 v0)。
 * 等待=设备完成中断(vSPI 35):等待期无他跑者(客户端阻塞在
 * 回执),WFI 由 tick IRQ 兜醒(≤1 拍延迟,与 vm1 演示同形)。 */

#define BLK_OCB_MAX 4u

struct blk_ocb {
    u32 used;
    u32 refs;
    u64 off;                       /* 字节位(裸盘=512 倍界内自由) */
};

static struct blk_ocb blk_ocbs[BLK_OCB_MAX];
static char blk_sec[512];          /* 扇区缓冲(驻本文件 bss=IPA 真) */

static struct blk_ocb *blk_ocb_alloc(void)
{
    u32 i;

    for (i = 0; i < BLK_OCB_MAX; i++)
        if (!blk_ocbs[i].used) {
            blk_ocbs[i].used = 1;
            blk_ocbs[i].refs = 1;
            blk_ocbs[i].off = 0;
            return &blk_ocbs[i];
        }
    return NULL;
}

static struct blk_ocb *blk_ocb_get(u32 id)
{
    return id && id <= BLK_OCB_MAX && blk_ocbs[id - 1].used
        ? &blk_ocbs[id - 1] : NULL;
}

/* 服务器主循环:READ(窗切)/WRITE(读改写)/SEEK(whence
 * 三档)/CLOSE/DUP;OPEN 由 fdcreate 同径(fdopen 之 name_attach
 * 面)。 */
static void srv_blkdev(void *arg)
{
    struct rt_fdreq req;
    struct rt_recvinfo ri;
    int chid;
    long r;

    (void)arg;
    chid = rt_msg_ch_create(0);
    if (chid <= 0)
        return;
    if (rt_msg_name_attach("/dev/blk", chid) != 0) {
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
            struct blk_ocb *o = blk_ocb_alloc();

            r = o ? (long)(o - blk_ocbs) + 1 : -12;
            break;
        }
        case RT_FDOP_READ: {
            struct blk_ocb *o = blk_ocb_get(req.ocb);
            u32 insec = (u32)(o ? o->off % 512 : 0);
            u32 n = req.a;

            if (!o || n > RT_FD_RDATA || o->off >= 64 * 512) {
                r = o ? -22 : -9;
                break;
            }
            if (insec + n > 512) {
                r = -22;                  /* 跨扇界:单窗单扇 v0 */
                break;
            }
            if (rt_blk_rw(0, o->off / 512, blk_sec, 50) != 0) {
                r = -5;                   /* -EIO:设备拒/越界 */
                break;
            }
            {
                u32 i;

                for (i = 0; i < n; i++)
                    req.data[i] = blk_sec[insec + i];
            }
            o->off += n;
            rt_msg_reply(chid, ri.rcvid, req.data, n, (long)n);
            continue;                     /* READ 走 rbuf 面自答自续 */
        }
        case RT_FDOP_WRITE: {
            struct blk_ocb *o = blk_ocb_get(req.ocb);
            u32 n = req.a > RT_FD_DATA ? RT_FD_DATA : req.a;
            u32 insec = (u32)(o ? o->off % 512 : 0);

            if (!o) {
                r = -9;
                break;
            }
            if (o->off >= 64 * 512 || insec + n > 512) {
                r = -22;
                break;
            }
            if (rt_blk_rw(0, o->off / 512, blk_sec, 50) != 0) {
                r = -5;
                break;
            }
            {   /* 读改写:扇区入缓冲,窗内补 req.a 字节,整扇写回 */
                u32 i;

                for (i = 0; i < n; i++)
                    blk_sec[insec + i] = req.data[i];
                if (rt_blk_rw(1, o->off / 512, blk_sec, 50) != 0) {
                    r = -5;
                    break;
                }
            }
            o->off += n;
            r = (long)n;
            break;
        }
        case RT_FDOP_SEEK: {
            struct blk_ocb *o = blk_ocb_get(req.ocb);

            if (!o) {
                r = -9;
                break;
            }
            if (req.rsv == 1u)
                o->off += req.a;         /* SEEK_CUR */
            else if (req.rsv == 2u)
                o->off = 64 * 512 - req.a > 64 * 512 ? 0 : 64 * 512 - req.a;
            else
                o->off = req.a;          /* SEEK_SET(默认,站33 契约) */
            r = (long)o->off;
            break;
        }
        case RT_FDOP_CLOSE: {
            struct blk_ocb *o = blk_ocb_get(req.ocb);

            if (!o) {
                r = -9;
                break;
            }
            if (o->refs)
                o->refs--;
            if (!o->refs)
                o->used = 0;
            r = 0;
            break;
        }
        case RT_FDOP_DUP: {
            struct blk_ocb *o = blk_ocb_get(req.ocb);

            if (!o) {
                r = -9;
                break;
            }
            o->refs++;
            r = 0;
            break;
        }
        default:
            r = -22;
        }
        rt_msg_reply(chid, ri.rcvid, NULL, 0, r);
    }
}

/* 驱动面:本文件独占 guest/virtio.h 的队列静态(g_hdr/g_data
 * 由参数化形替代;rt_main 已退役其 virtio 含头与探针,单 TU
 * 契约免双队列)。ticks=回执等待窗(WFI 由 tick IRQ 兜醒)。 */
int rt_blk_rw(u32 type, u64 sector, void *buf512, u32 ticks)
{
    u64 deadline = ticks ? rt_ticks + ticks : 0;
    u32 st;

    (void)deadline;                       /* WFI 形:窗由 tick 分段醒 */
    st = virtio_blk_do2(type, sector, buf512);
    return st == 0 ? 0 : -5;
}

/* 两相:探针相(打印锚序不变,rt_main 原 slot 调用)/服务器相
 * (IRQ 登记+blksrv 起立,rt_srv_boot 序内)。 */
void rt_blk_boot(void)
{
    virtio_blk_init();                    /* probe 三验+队列+DRIVER_OK */
}

void rt_blk_srv_boot(void)
{
    rt_irq_register(GUEST_INTID_VIRTIO, virtio_isr, 0);
    if (!rt_task_create("blksrv", srv_blkdev, NULL, 8))
        rt_puts("srv: blksrv spawn failed\n");
}
