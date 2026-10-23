/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#ifndef ESHINE_VBLK_H
#define ESHINE_VBLK_H

#include <stdbool.h>

#include "kernel/include/types.h"

struct eshine_vm;                          /* 前向声明(vm.h 反向含本头) */

/* 27.04 第九站:RAM 磁盘块设备 vblk。内存即传输面,寄存器即门铃
 * (virtqueue 种子):数据缓冲在 guest 自有 RAM(它是唯一写者),宿主
 * 按 mem[] 静态映射翻译直访其物理内存;MMIO 只承载门铃/状态。
 * 服务在 CMD 陷阱内同步完成(RAM 介质无真实延迟),异步的只有完成
 * 中断(vSPI 34,LR2 通道)。参数错置 err 照常完成——设备级默认
 * 拒绝,不杀 guest。磁盘为静态数组,不经 vm_create 复位:
 * 销毁重建数据保留是结构保证。 */

#define VBLK_BASE      0x0A000000UL    /* 仿真窗口(IPA) */
#define VBLK_SECTORS   128UL           /* 128 扇区 x 512B = 64KB/VM */

struct vblk {
    u32 addr;                /* 数据缓冲 IPA(guest RAM,512B 内含) */
    u32 ris;                 /* 完成挂起(bit0) */
    u32 imsc;                 /* 完成中断使能(bit0) */
    u32 done;                 /* STATUS.done 位(读清) */
    u32 err;                 /* STATUS.err 位(读清) */
};

void vblk_mmio_write(struct eshine_vm *vm, u32 vmid, u64 off, u64 val);
u64  vblk_mmio_read(struct eshine_vm *vm, u32 vmid, u64 off);
bool vblk_irq_ready(const struct eshine_vm *vm);

#endif /* ESHINE_VBLK_H */
