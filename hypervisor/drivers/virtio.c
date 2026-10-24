/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
/* virtio:virtio-mmio legacy + split virtqueue + virtio-blk(第十站) */
#include "drivers/include/virtio.h"
#include "kernel/include/util.h"
#include "kernel/include/vm.h"

#define PAGE 4096UL

/* virtqueue split 布局(legacy 公式,宿主与 guest 驱动两侧自洽):
 *   desc 表:基址(QueuePFN<<12)起 16N 字节
 *   avail 环:紧随 desc(2 对齐):u16 flags/idx + N x u16 ring + u16 used_event
 *   used 环:avail 后 4096 对齐:u16 flags/idx + N x {u32 id,u32 len} + u16 avail_event */
struct vq_desc {
    u64 addr;
    u32 len;
    u16 flags;
    u16 next;
};

static u64 desc_off(void)     { return 0UL; }
static u64 avail_off(u32 num) { return 16UL * num; }
static u64 used_off(u32 num)
{
    return (avail_off(num) + 6UL + 2UL * num + (PAGE - 1)) & ~(PAGE - 1);
}

/* 静态磁盘(承接第九站 vblk 的数组,迁移即结构连续:自制协议退役,
 * 行业标准接管,数据一个字节不动)。不经 vm_create 复位——销毁
 * 重建数据保留是结构保证。 */
static u8 virtio_disk[ESHINE_MAX_VMS][VIRTIO_SECTORS * 512UL];

/* used 环视图(定长转译,深度上限 VIRTIO_QUEUE_MAX) */
struct vq_used {
    u16 flags;
    u16 idx;
    struct { u32 id; u32 len; } e[VIRTIO_QUEUE_MAX];
    u16 avail_event;
};

/* 三描述符链服务:header(type/sector) + data(512B) + status(1B)。
 * 参数越界:status=ERR 照常完成(设备级默认拒绝,不杀 guest)。
 * 畸形链(断链/翻译失败):跳过该请求——单 CPU 同步服务,无竞态。 */
static void virtio_process_avail(struct eshine_vm *vm, u32 vmid)
{
    struct virtio_mmio *vt = &vm->virtio;
    u32 n = vt->queue_num;
    u64 base = (u64)vt->queue_pfn << 12;
    struct vq_desc *desc;
    u16 *avail;
    struct vq_used *used;
    u16 avail_idx;

    if (n == 0u || vt->queue_pfn == 0u)
        return;
    desc = (struct vq_desc *)vm_ipa_to_pa(vm, base + desc_off(), 16UL * n);
    avail = (u16 *)vm_ipa_to_pa(vm, base + avail_off(n), 6UL + 2UL * n);
    used = (struct vq_used *)vm_ipa_to_pa(vm, base + used_off(n),
                                          6UL + 8UL * n);
    if (desc == NULL || avail == NULL || used == NULL)
        return;

    avail_idx = avail[1];                    /* avail->idx */

    /* 门铃语义:把宿主消费点推进到 avail->idx(批量清) */
    while (vt->last_avail != avail_idx) {
        u16 head = avail[2 + (vt->last_avail & (n - 1))];
        struct vq_desc *d;
        u8 *hdr, *data, *stat;
        u32 type, sector, len;
        bool err = false;

        /* ①header 16B(type/rsvd/sector) */
        d = &desc[head];
        if (!(d->flags & VIRTQ_DESC_F_NEXT))
            goto skip;
        hdr = (u8 *)vm_ipa_to_pa(vm, d->addr, 16UL);
        if (hdr == NULL)
            goto skip;
        type = *(u32 *)(void *)hdr;
        sector = *(u32 *)(void *)(hdr + 8);

        /* ②data */
        d = &desc[d->next];
        if (!(d->flags & VIRTQ_DESC_F_NEXT))
            goto skip;
        data = (u8 *)vm_ipa_to_pa(vm, d->addr, d->len);
        if (data == NULL || d->len == 0UL)
            goto skip;
        len = d->len;

        /* ③status 1B */
        d = &desc[d->next];
        stat = (u8 *)vm_ipa_to_pa(vm, d->addr, 1UL);
        if (stat == NULL)
            goto skip;

        /* 扇区 I/O:同一静态磁盘(vblk 时代的数组) */
        if (sector >= VIRTIO_SECTORS || type > 1u) {
            err = true;
        } else {
            u8 *sec = virtio_disk[vmid] + (u64)sector * 512UL;
            u32 m = len < 512u ? len : 512u;

            if (type == 0u)                  /* IN:设备->guest(读) */
                memcpy(data, sec, m);
            else                            /* OUT:guest->设备(写) */
                memcpy(sec, data, m);
        }
        *stat = err ? 1u : 0u;

        /* used 环 append */
        used->e[used->idx & (n - 1)].id = head;
        used->e[used->idx & (n - 1)].len = 512UL;
        used->idx++;
skip:
        vt->last_avail++;
    }
    vt->int_pend = 1u;                       /* used 环更新:完成挂起 */
}

void virtio_mmio_write(struct eshine_vm *vm, u32 vmid, u64 off, u64 val)
{
    struct virtio_mmio *vt = &vm->virtio;

    switch (off) {
    case VIRTIO_REG_DRV_FEATURES:
        vt->drv_features = (u32)val;        /* 本站零特性:接受即记账 */
        return;
    case VIRTIO_REG_DRV_FEAT_SEL:
        return;                             /* 仅 0 字有效,不记账 */
    case VIRTIO_REG_QUEUE_SEL:
        vt->queue_sel = (u32)val;           /* 单队列:非 0 视无效 */
        return;
    case VIRTIO_REG_QUEUE_NUM:
        if (((u32)val & ((u32)val - 1u)) == 0u && (u32)val <= VIRTIO_QUEUE_MAX)
            vt->queue_num = (u32)val;
        else
            vt->status |= VIRTIO_STATUS_FAILED;
        return;
    case VIRTIO_REG_QUEUE_ALIGN:           /* legacy:布局公式按 4096 固化,
                                             * 他值即 FAILED(诚实拒绝) */
        if ((u32)val == 4096u)
            vt->queue_align = (u32)val;
        else
            vt->status |= VIRTIO_STATUS_FAILED;
        return;
    case VIRTIO_REG_QUEUE_PFN:
        vt->queue_pfn = (u32)val;
        vt->last_avail = 0;                 /* 新队列:消费点归零 */
        return;
    case VIRTIO_REG_QUEUE_NOTIFY:
        if ((u32)val == 0u)
            virtio_process_avail(vm, vmid);
        return;
    case VIRTIO_REG_INT_ACK:
        vt->int_pend &= ~((u32)val & 1u);
        return;
    case VIRTIO_REG_STATUS:
        vt->status = (u32)val & 0xFFu;      /* guest 写全量:含复位 */
        return;
    default:
        return;                             /* 只读寄存器写忽略 */
    }
}

u64 virtio_mmio_read(struct eshine_vm *vm, u32 vmid, u64 off)
{
    struct virtio_mmio *vt = &vm->virtio;

    (void)vmid;
    switch (off) {
    case VIRTIO_REG_MAGIC:
        return 0x74726976UL;                /* 'v','i','r','t' */
    case VIRTIO_REG_VERSION:
        return 1UL;                         /* legacy 接口(v1 真身) */
    case VIRTIO_REG_DEVICE_ID:
        return 2UL;                         /* block */
    case VIRTIO_REG_VENDOR_ID:
        return 0x4553UL;                    /* 'ES' */
    case VIRTIO_REG_DEV_FEATURES:
        return 0UL;                         /* 零特性位(读写已含) */
    case VIRTIO_REG_DEV_FEAT_SEL:
        return 0UL;
    case VIRTIO_REG_QUEUE_SEL:
        return vt->queue_sel;
    case VIRTIO_REG_QUEUE_NUM_MAX:
        return VIRTIO_QUEUE_MAX;
    case VIRTIO_REG_QUEUE_PFN:
        return vt->queue_pfn;
    case VIRTIO_REG_INT_STATUS:
        return vt->int_pend & 1UL;
    case VIRTIO_REG_STATUS:
        return vt->status;
    case VIRTIO_REG_QUEUE_ALIGN:
        return vt->queue_align;
    default:
        if (off >= VIRTIO_REG_CONFIG && off < VIRTIO_REG_CONFIG + 8) {
            /* virtio-blk 配置区:capacity(u64 LE,扇区数)。
             * 越界读移出,防 shift>=64 UB */
            u64 cap = VIRTIO_SECTORS;
            return cap >> ((off - VIRTIO_REG_CONFIG) * 8);
        }
        return 0UL;
    }
}

bool virtio_irq_ready(const struct eshine_vm *vm)
{
    /* DRIVER_OK 兼任中断使能(virtio-mmio 无 IMSC) */
    return (vm->virtio.status & VIRTIO_STATUS_DRIVER_OK) != 0u &&
           (vm->virtio.int_pend & 1u) != 0u;
}
