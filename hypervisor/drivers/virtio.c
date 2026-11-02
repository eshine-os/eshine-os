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

/* ---- 站五十八:virtio-net(设备 id 1;OASIS virtio 1.1 spec clean-room) ----
 * 回环后端:tx kick 时读 tx 链(hdr 10B 跳过+帧数据入设备暂存区),再
 * 拷进 guest 预投的 rx 单描述符缓冲(帧前留 10B net 头),双 used 环
 * 同拍完成+int_pend。rx kick=v0 无待灌帧(guest 补缓冲,无积压);
 * rx 缓冲未投=丢帧计数(诚实计数器记边界)。
 *
 * **ARP 回声丢弃(v0 平台边界)**:ethtype 0x0806 的回环递送丢弃
 * (tx 完成照常)。定谳矩阵(九刀二分+插桩,站 53 MDCR/TCG 同例):
 * 撤 vIRQ=活;IRQ+空 poll=活;rx 完成无帧=活;零帧=活;IP ethtype=活;
 * ARP(请求/应答,自/他 IP,单播/广播)=死(6.6 Oops 于
 * __netif_receive_skb_core+0x4 执行 FDT 字节,ESR 0x02000000——
 * QEMU 未实现寄存器同号异常;host 侧 gdump 物理文本净且崩前崩后
 * 不变=客 stage-1/我 stage-2 映射分歧,机制未穿透)。
 * 真实 NIC 也不收自家出向广播——回环回声本就病态;RX 递送的
 * 完整证明走原路设计(宿主侧 QEMU -netdev 桥接弧,roadmap E6)。 */

#define VIRTIO_MAX_FRAME 1526u         /* 以太网最大帧(v0 MTU 边界) */

static u8 vnet_frame[VIRTIO_MAX_FRAME];
static u32 vnet_drop_count;

static void virtio_net_process_tx(struct eshine_vm *vm)
{
    struct virtio_net *vn = &vm->vnet;
    u32 n = vn->queue_num[VIRTIO_NET_TX];
    u64 base = (u64)vn->queue_pfn[VIRTIO_NET_TX] << 12;
    struct vq_desc *desc, *rdesc;
    u16 *avail, *ravail;
    struct vq_used *used, *rused;
    u16 avail_idx;

    if (n == 0u || vn->queue_pfn[VIRTIO_NET_TX] == 0u)
        return;
    desc = (struct vq_desc *)vm_ipa_to_pa(vm, base + desc_off(), 16UL * n);
    avail = (u16 *)vm_ipa_to_pa(vm, base + avail_off(n), 6UL + 2UL * n);
    used = (struct vq_used *)vm_ipa_to_pa(vm, base + used_off(n),
                                          6UL + 8UL * n);
    if (desc == NULL || avail == NULL || used == NULL)
        return;

    avail_idx = avail[1];
    while (vn->last_avail[VIRTIO_NET_TX] != avail_idx) {
        u16 head = avail[2 + (vn->last_avail[VIRTIO_NET_TX] & (n - 1))];
        struct vq_desc *d = &desc[head];
        u8 *rxbuf = NULL;
        u16 rxhead = 0;
        u32 copied = 0;
        bool arp = false;

        /* tx 链走读:首描述符=net 头(10B 跳过),续描述符=帧数据(gather);
         * ethertype 探头(ARP 判定=回声丢弃边界) */
        while (d->flags & VIRTQ_DESC_F_NEXT) {
            struct vq_desc *nd = &desc[d->next];

            if (nd->len != 0UL) {
                u8 *src = (u8 *)vm_ipa_to_pa(vm, nd->addr, nd->len);

                if (src != NULL) {
                    if (copied == 0u && nd->len >= 14u &&
                        src[12] == 0x08u && src[13] == 0x06u)
                        arp = true;
                    if (rxbuf != NULL && copied < VIRTIO_MAX_FRAME) {
                        u32 m = nd->len > VIRTIO_MAX_FRAME - copied
                                    ? VIRTIO_MAX_FRAME - copied : nd->len;

                        memcpy(vnet_frame + copied, src, m);
                        copied += m;
                    }
                }
            }
            d = nd;
        }

        /* rx 缓冲:guest 预投单描述符(hdr+帧窗口);ARP 回声不递送 */
        if (!arp) {
            u32 rn = vn->queue_num[VIRTIO_NET_RX];
            u64 rbase = (u64)vn->queue_pfn[VIRTIO_NET_RX] << 12;

            if (rn != 0u && vn->queue_pfn[VIRTIO_NET_RX] != 0u) {
                rdesc = (struct vq_desc *)vm_ipa_to_pa(vm, rbase + desc_off(),
                                                       16UL * rn);
                ravail = (u16 *)vm_ipa_to_pa(vm, rbase + avail_off(rn),
                                              6UL + 2UL * rn);
                rused = (struct vq_used *)vm_ipa_to_pa(vm, rbase + used_off(rn),
                                                        6UL + 8UL * rn);
                if (rdesc != NULL && ravail != NULL && rused != NULL &&
                    rdesc->len > VIRTIO_NET_HDR &&
                    copied <= rdesc->len - VIRTIO_NET_HDR &&
                    vn->last_avail[VIRTIO_NET_RX] != ravail[1]) {
                    rxhead = ravail[2 + (vn->last_avail[VIRTIO_NET_RX] &
                                         (rn - 1))];
                    rxbuf = (u8 *)vm_ipa_to_pa(vm, rdesc[rxhead].addr,
                                                rdesc[rxhead].len);
                    if (rxbuf != NULL) {
                        u32 cap = (u32)rdesc[rxhead].len -
                                  (u32)VIRTIO_NET_HDR;

                        if (copied > cap)
                            copied = cap;
                        for (u32 i = 0; i < VIRTIO_NET_HDR; i++)
                            rxbuf[i] = 0u;   /* 净头:无 offload 诚形 */
                        memcpy(rxbuf + VIRTIO_NET_HDR, vnet_frame, copied);
                        rused->e[rused->idx & (rn - 1u)].id = rxhead;
                        rused->e[rused->idx & (rn - 1u)].len =
                            (u32)VIRTIO_NET_HDR + copied;
                        rused->idx++;
                        vn->last_avail[VIRTIO_NET_RX]++;
                    }
                }
            }
        }
        if (arp || rxbuf == NULL)
            vnet_drop_count++;         /* ARP 回声/rx 缓冲未投:丢帧记数 */
        used->e[used->idx & (n - 1)].id = head;
        used->e[used->idx & (n - 1)].len = (u32)VIRTIO_NET_HDR + copied;
        used->idx++;
        vn->last_avail[VIRTIO_NET_TX]++;
    }
    vn->int_pend = 1u;
}

void virtio_net_mmio_write(struct eshine_vm *vm, u32 vmid, u64 off, u64 val)
{
    struct virtio_net *vn = &vm->vnet;

    (void)vmid;
    switch (off) {
    case VIRTIO_REG_DRV_FEATURES:
        vn->drv_features = (u32)val & (u32)VIRTIO_NET_F_MAC;  /* 只收 MAC 位 */
        return;
    case VIRTIO_REG_DRV_FEAT_SEL:
        return;
    case VIRTIO_REG_QUEUE_SEL:
        vn->queue_sel = (u32)val;           /* 0 rx/1 tx;他值选不中队 */
        return;
    case VIRTIO_REG_QUEUE_NUM:
        if (((u32)val & ((u32)val - 1u)) == 0u && (u32)val <= VIRTIO_QUEUE_MAX &&
            vn->queue_sel < VIRTIO_NET_QUEUES)
            vn->queue_num[vn->queue_sel] = (u32)val;
        else
            vn->status |= VIRTIO_STATUS_FAILED;
        return;
    case VIRTIO_REG_QUEUE_ALIGN:
        if ((u32)val == 4096u)
            vn->queue_align = (u32)val;
        else
            vn->status |= VIRTIO_STATUS_FAILED;
        return;
    case VIRTIO_REG_QUEUE_PFN:
        if (vn->queue_sel < VIRTIO_NET_QUEUES) {
            vn->queue_pfn[vn->queue_sel] = (u32)val;
            vn->last_avail[vn->queue_sel] = 0;
        }
        return;
    case VIRTIO_REG_QUEUE_NOTIFY:
        if ((u32)val == VIRTIO_NET_TX)
            virtio_net_process_tx(vm);
        return;                             /* rx kick:v0 无待灌帧 */
    case VIRTIO_REG_INT_ACK:
        vn->int_pend &= ~((u32)val & 1u);
        return;
    case VIRTIO_REG_STATUS:
        vn->status = (u32)val & 0xFFu;
        return;
    default:
        return;
    }
}

u64 virtio_net_mmio_read(struct eshine_vm *vm, u32 vmid, u64 off)
{
    static const u8 vnet_mac[6] = { 0x52, 0x55, 0x0a, 0x00, 0x02, 0x58 };
    struct virtio_net *vn = &vm->vnet;

    (void)vmid;
    switch (off) {
    case VIRTIO_REG_MAGIC:
        return 0x74726976UL;
    case VIRTIO_REG_VERSION:
        return 1UL;                         /* legacy(v1 真身,与 blk 同律) */
    case VIRTIO_REG_DEVICE_ID:
        return 1UL;                         /* net(OASIS §5.1.2) */
    case VIRTIO_REG_VENDOR_ID:
        return 0x4553UL;                    /* 'ES' */
    case VIRTIO_REG_DEV_FEATURES:
        return VIRTIO_NET_F_MAC;            /* 唯一特性:配置区 MAC */
    case VIRTIO_REG_DEV_FEAT_SEL:
        return 0UL;
    case VIRTIO_REG_QUEUE_SEL:
        return vn->queue_sel;
    case VIRTIO_REG_QUEUE_NUM_MAX:
        return VIRTIO_QUEUE_MAX;
    case VIRTIO_REG_QUEUE_PFN:
        return vn->queue_sel < VIRTIO_NET_QUEUES
                   ? vn->queue_pfn[vn->queue_sel] : 0UL;
    case VIRTIO_REG_INT_STATUS:
        return vn->int_pend & 1UL;
    case VIRTIO_REG_STATUS:
        return vn->status;
    case VIRTIO_REG_QUEUE_ALIGN:
        return vn->queue_align;
    default:
        if (off >= VIRTIO_REG_CONFIG && off < VIRTIO_REG_CONFIG + 6) {
            /* net 配置区唯一字段:MAC 6B(OASIS §5.1.4+特性位 5 */
            return (u64)vnet_mac[off - VIRTIO_REG_CONFIG];
        }
        return 0UL;
    }
}

bool virtio_net_irq_ready(const struct eshine_vm *vm)
{
    return (vm->vnet.status & VIRTIO_STATUS_DRIVER_OK) != 0u &&
           (vm->vnet.int_pend & 1u) != 0u;
}
