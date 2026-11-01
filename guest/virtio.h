/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#ifndef GUEST_VIRTIO_H
#define GUEST_VIRTIO_H

typedef unsigned long long u64;
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;

/* 第十站:virtio-mmio legacy + split virtqueue + virtio-blk 客户驱动。
 * probe 三验 → Status 步进 → 队列就位 → DRIVER_OK → 三描述符链
 * 请求 → 门铃 → WFI 等完成中断(vSPI 35)。访存器全走 volatile asm
 * 访问器(ISV=1 契约,第八站教训)。 */
#define VIRTIO_BASE 0x0A000000UL

/* 寄存器偏移(与宿主 virtio.h 同表) */
#define VR_MAGIC          0x00u
#define VR_VERSION        0x04u
#define VR_DEVICE_ID      0x08u
#define VR_DEV_FEATURES   0x10u
#define VR_DRV_FEATURES   0x20u
#define VR_QUEUE_NUM      0x38u
#define VR_QUEUE_ALIGN    0x3Cu        /* legacy:used 环对齐(4096) */
#define VR_QUEUE_PFN      0x40u
#define VR_QUEUE_NOTIFY   0x50u
#define VR_INT_STATUS     0x60u
#define VR_INT_ACK        0x64u
#define VR_STATUS         0x70u

#define VIRTIO_MAGIC     0x74726976u
#define VIRTIO_VERSION   1u              /* v1=legacy 接口(第十四站修正) */
#define VIRTIO_DEVID_BLK 2u

#define ST_ACKNOWLEDGE 0x01u
#define ST_DRIVER     0x02u
#define ST_DRIVER_OK  0x04u

#define VIRTQ_F_NEXT   0x01u

static inline u32 vr_read(unsigned long off)
{
    unsigned long addr = VIRTIO_BASE + off;
    u32 v;

    __asm__ volatile("ldr %w0, [%1]" : "=r"(v) : "r"(addr));
    return v;
}

static inline void vr_write(unsigned long off, u32 v)
{
    unsigned long addr = VIRTIO_BASE + off;

    __asm__ volatile("str %w0, [%1]" :: "r"(v), "r"(addr) : "memory");
}

/* ---- 队列(legacy 布局,与宿主公式同表) ---- */
#define VRING_NUM 8u
#define VRING_MASK (VRING_NUM - 1u)

struct vq_desc {
    u64 addr;
    u32 len;
    u16 flags;
    u16 next;
};

struct vq_avail {
    u16 flags;
    u16 idx;
    u16 ring[VRING_NUM];
    u16 used_event;
};

struct vq_used {
    u16 flags;
    u16 idx;
    struct { u32 id; u32 len; } e[VRING_NUM];
    u16 avail_event;
};

/* 队列区:单块 8KB,按 legacy 布局公式落位(宿主同表)——不依赖
 * 链接器摆放:desc@+0,avail@+128(16N),used@+4096(对齐公式) */
static char g_vq[8192] __attribute__((aligned(4096)));
#define g_desc  ((struct vq_desc *)(g_vq + 0))
#define g_avail ((struct vq_avail *)(g_vq + 128))
#define g_used  ((struct vq_used *)(g_vq + 4096))

/* virtio-blk 请求(三描述符链):header 16B + data 512B + status 1B */
struct blk_req {
    u32 type;                /* 0=读(IN) 1=写(OUT) */
    u32 rsvd;
    u64 sector;
};

static struct blk_req g_hdr;
static char g_data[512];
static u8 g_stat;
static volatile unsigned int virtio_done;   /* ISR 置位(完成事件) */

/* guest RAM 队列区基址:desc 起,used 在其后 4096 对齐(静态布局即满足) */
static inline void virtio_blk_init(void)
{
    u32 st;

    /* probe 三验:magic/version/deviceID */
    if (vr_read(VR_MAGIC) != VIRTIO_MAGIC ||
        vr_read(VR_VERSION) != VIRTIO_VERSION ||
        vr_read(VR_DEVICE_ID) != VIRTIO_DEVID_BLK)
        return;

    st = vr_read(VR_STATUS);
    st |= ST_ACKNOWLEDGE | ST_DRIVER;
    vr_write(VR_STATUS, st);

    vr_write(VR_DRV_FEATURES, 0);           /* 零特性:读写已够 */
    vr_write(VR_QUEUE_NUM, VRING_NUM);
    vr_write(VR_QUEUE_ALIGN, 4096u);       /* legacy 契约:先对齐后基址 */
    /* QueuePFN = 队列区基址页号(desc 起;used 由布局公式导出) */
    vr_write(VR_QUEUE_PFN, (u32)((unsigned long)g_vq >> 12));

    st = vr_read(VR_STATUS);
    st |= ST_DRIVER_OK;
    vr_write(VR_STATUS, st);
    g_avail->idx = 0;
    g_used->idx = 0;
}

/* 提交:type(0 读/1 写) + sector;三链入队 → 门铃 → WFI 等完成中断 */
static inline void virtio_blk_do(u32 type)
{
    u16 idx = g_avail->idx & VRING_MASK;

    g_hdr.type = type;
    g_hdr.rsvd = 0;
    g_hdr.sector = 7;                        /* 演示扇区(承接 vblk 时代) */
    g_stat = 0xff;

    g_desc[0].addr = (u64)(unsigned long)&g_hdr;
    g_desc[0].len = sizeof(g_hdr);
    g_desc[0].flags = VIRTQ_F_NEXT;
    g_desc[0].next = 1;
    g_desc[1].addr = (u64)(unsigned long)g_data;
    g_desc[1].len = 512;
    g_desc[1].flags = VIRTQ_F_NEXT;
    g_desc[1].next = 2;
    g_desc[2].addr = (u64)(unsigned long)&g_stat;
    g_desc[2].len = 1;
    g_desc[2].flags = 0;
    g_desc[2].next = 0;

    g_avail->ring[idx] = 0;                   /* 链头 = desc 0 */
    g_avail->idx++;

    vr_write(VR_QUEUE_NOTIFY, 0);            /* 门铃 */

    while (!virtio_done)
        __asm__ volatile("wfi" ::: "memory");
    virtio_done = 0;
}

/* 站39:参数化面(sector+缓冲自择;buf512 须驻本客静态区=IPA 真
 * 值;服务器自备 512B 扇区缓冲经此换装)。WFI 等法同上:单核客
 * 上等待期无他跑者(客户端阻塞在回执 sem),tick IRQ 兜醒。 */
static inline u32 virtio_blk_do2(u32 type, u64 sector, void *buf512)
{
    u16 idx = g_avail->idx & VRING_MASK;

    g_hdr.type = type;
    g_hdr.rsvd = 0;
    g_hdr.sector = sector;
    g_stat = 0xff;

    g_desc[0].addr = (u64)(unsigned long)&g_hdr;
    g_desc[0].len = sizeof(g_hdr);
    g_desc[0].flags = VIRTQ_F_NEXT;
    g_desc[0].next = 1;
    g_desc[1].addr = (u64)(unsigned long)buf512;
    g_desc[1].len = 512;
    g_desc[1].flags = VIRTQ_F_NEXT;
    g_desc[1].next = 2;
    g_desc[2].addr = (u64)(unsigned long)&g_stat;
    g_desc[2].len = 1;
    g_desc[2].flags = 0;
    g_desc[2].next = 0;

    g_avail->ring[idx] = 0;
    g_avail->idx++;

    vr_write(VR_QUEUE_NOTIFY, 0);

    while (!virtio_done)
        __asm__ volatile("wfi" ::: "memory");
    virtio_done = 0;
    return (u32)g_stat;                /* 0=成 1=越界/拒(设备级) */
}

static inline void virtio_isr(void)          /* IRQ 上下文(vSPI 35) */
{
    if (vr_read(VR_INT_STATUS) & 1u) {
        vr_write(VR_INT_ACK, 1u);            /* 清挂起 */
        virtio_done++;
    }
}

#endif /* GUEST_VIRTIO_H */
