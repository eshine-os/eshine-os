/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#ifndef ESHINE_VIRTIO_H
#define ESHINE_VIRTIO_H

#include <stdbool.h>

#include "kernel/include/types.h"

struct eshine_vm;                          /* 前向声明(vm.h 反向含本头) */

/* 27.04 第十站:virtio-mmio legacy + split virtqueue + virtio-blk。
 * 第九站 vblk 的"virtqueue 种子"长成行业标准设备:特性协商
 * (最小集)/描述符链/avail-used 双环/通知门铃/完成中断(vSPI 35,
 * LR3)。自制 vblk 协议退役,本设备**承接同一静态磁盘**——迁移即
 * 结构连续。Status.DRIVER_OK 兼任中断使能位(virtio-mmio 无 IMSC)。 */

#define VIRTIO_BASE       0x0A000000UL     /* 窗口(承接 vblk 槽位) */
#define VIRTIO_SECTORS    128UL            /* 128 扇区 x 512B = 64KB/VM */
#define VIRTIO_QUEUE_MAX  8UL              /* QueueNumMax:深度 8(2 的幂) */

/* 寄存器偏移(virtio-mmio legacy,v1 真身——第十四站修正:站十自洽期
 * 以 v2 号配 legacy 布局,Linux 按 v2 走现代布局会踩空 QueuePFN;
 * v1=legacy 是规范本义,自家驱动与 Linux 同契约) */
#define VIRTIO_REG_MAGIC          0x00UL   /* R:0x74726976('virt') */
#define VIRTIO_REG_VERSION       0x04UL   /* R:1 = legacy 接口 */
#define VIRTIO_REG_DEVICE_ID      0x08UL   /* R:2 = block */
#define VIRTIO_REG_VENDOR_ID      0x0CUL   /* R:0x4553('ES') */
#define VIRTIO_REG_DEV_FEATURES   0x10UL   /* R:host 特性位(本站恒 0) */
#define VIRTIO_REG_DEV_FEAT_SEL   0x14UL   /* RW:特性字选择(仅 0) */
#define VIRTIO_REG_DRV_FEATURES   0x20UL   /* W:guest 接受的特性 */
#define VIRTIO_REG_DRV_FEAT_SEL   0x24UL   /* W */
#define VIRTIO_REG_QUEUE_SEL      0x30UL   /* RW:单队列,仅 0 */
#define VIRTIO_REG_QUEUE_NUM_MAX  0x34UL   /* R:8 */
#define VIRTIO_REG_QUEUE_NUM      0x38UL   /* W:队列深度(2 的幂,<=8) */
#define VIRTIO_REG_QUEUE_ALIGN    0x3CUL   /* W:legacy used 环对齐(仅 4096) */
#define VIRTIO_REG_QUEUE_PFN      0x40UL   /* RW:legacy 队列基址页号 */
#define VIRTIO_REG_CONFIG         0x100UL  /* R:virtio-blk 配置区(capacity) */
#define VIRTIO_REG_QUEUE_NOTIFY   0x50UL   /* W:门铃(队列号 0) */
#define VIRTIO_REG_INT_STATUS     0x60UL   /* R:bit0 = used 环更新 */
#define VIRTIO_REG_INT_ACK        0x64UL   /* W:写清 */
#define VIRTIO_REG_STATUS         0x70UL   /* RW:ACK/DRIVER/DRIVER_OK/FAILED */

/* Status 位(virtio 规范) */
#define VIRTIO_STATUS_ACKNOWLEDGE  0x01u
#define VIRTIO_STATUS_DRIVER      0x02u
#define VIRTIO_STATUS_DRIVER_OK   0x04u
#define VIRTIO_STATUS_FAILED      0x80u

/* 描述符(virtqueue split,legacy) */
#define VIRTQ_DESC_F_NEXT        0x01u

struct virtio_mmio {
    u32 status;
    u32 drv_features;
    u32 queue_pfn;              /* legacy 队列基址(guest RAM 页号) */
    u32 queue_align;            /* legacy used 环对齐(仅 4096,公式同构) */
    u32 queue_num;
    u32 queue_sel;
    u32 int_pend;               /* 完成挂起(bit0=used 环更新) */
    u16 last_avail;             /* 宿主消费位(avail->idx 的追赶点) */
};

void virtio_mmio_write(struct eshine_vm *vm, u32 vmid, u64 off, u64 val);
u64  virtio_mmio_read(struct eshine_vm *vm, u32 vmid, u64 off);
bool virtio_irq_ready(const struct eshine_vm *vm);

#endif /* ESHINE_VIRTIO_H */
