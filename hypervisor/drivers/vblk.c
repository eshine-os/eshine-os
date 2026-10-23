/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
/* vblk:RAM 磁盘块设备(门铃 + 完成中断),第九站 */
#include "drivers/include/vblk.h"
#include "kernel/include/util.h"
#include "kernel/include/vm.h"

/* 偏移(见 vblk.h) */
#define VBLK_CMD    0x00UL
#define VBLK_ADDR   0x04UL
#define VBLK_STATUS 0x08UL
#define VBLK_CAP    0x0CUL
#define VBLK_IMSC  0x10UL

#define BLK_OP_READ  1u
#define BLK_OP_WRITE 2u
#define IRQ_DONE     (1u << 0)

/* 静态磁盘:每 VM 槽位 64KB。不经 vm_create 复位——销毁(vmkill/
 * fault/exit)后重建,数据保留(结构保证,非代码路径)。 */
static u8 blk_disk[ESHINE_MAX_VMS][VBLK_SECTORS * 512UL];

/* 完成路径:置挂起+done/err;宿主按 imsc 决定泵注入(LR2 vSPI 34) */
static void vblk_complete(struct vblk *vb, bool err)
{
    vb->done = 1u;
    if (err)
        vb->err = 1u;
    vb->ris |= IRQ_DONE;
}

void vblk_mmio_write(struct eshine_vm *vm, u32 vmid, u64 off, u64 val)
{
    struct vblk *vb = &vm->vb;

    switch (off) {
    case VBLK_ADDR:
        vb->addr = (u32)val;
        return;
    case VBLK_IMSC:
        vb->imsc = (u32)val & IRQ_DONE;    /* 仅完成中断建模 */
        return;
    case VBLK_CMD: {
        u32 op = (u32)val & 0xFFu;
        u32 sec = ((u32)val >> 8) & 0xFFFFu;
        u64 pa;

        /* 同步服务(RAM 介质无延迟),异步的只有完成中断 */
        if (op != BLK_OP_READ && op != BLK_OP_WRITE) {
            vblk_complete(vb, true);
            return;
        }
        if (sec >= VBLK_SECTORS) {
            vblk_complete(vb, true);
            return;
        }
        pa = vm_ipa_to_pa(vm, vb->addr, 512UL);
        if (pa == 0UL) {                   /* 缓冲不落 guest RAM 区 */
            vblk_complete(vb, true);
            return;
        }
        if (op == BLK_OP_READ)
            memcpy((void *)pa, blk_disk[vmid] + (u64)sec * 512UL, 512UL);
        else
            memcpy(blk_disk[vmid] + (u64)sec * 512UL, (const void *)pa, 512UL);
        vblk_complete(vb, false);
        return;
    }
    default:
        return;                            /* STATUS/CAP 写忽略 */
    }
}

u64 vblk_mmio_read(struct eshine_vm *vm, u32 vmid, u64 off)
{
    struct vblk *vb = &vm->vb;

    (void)vmid;                    /* 读路径无磁盘访问(数据面在 CMD 服务) */

    switch (off) {
    case VBLK_STATUS: {
        /* 读清 done/err(guest ISR 以此清挂起);RIS 由读 STATUS 联动清
         * ——真设备同律:handler 读一次状态即消化完成事件。 */
        u32 v = (vb->done ? 1u : 0u) | (vb->err ? 2u : 0u);

        vb->done = 0u;
        vb->err = 0u;
        vb->ris &= ~IRQ_DONE;
        return v;
    }
    case VBLK_CAP:
        return (u64)VBLK_SECTORS;
    case VBLK_ADDR:
        return vb->addr;
    case VBLK_IMSC:
        return vb->imsc;
    default:
        return 0UL;                        /* CMD 读 0 */
    }
}

bool vblk_irq_ready(const struct eshine_vm *vm)
{
    return (vm->vb.imsc & vm->vb.ris & IRQ_DONE) != 0u;
}
