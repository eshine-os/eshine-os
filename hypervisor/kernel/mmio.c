/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
/* MMIO 分发:stage-2 未映射区访存陷阱 -> 仿真设备模型。
 * HPFAR_EL2 是 stage-2 fault 的 IPA 权威来源(FAR_EL2 对 S2 fault 无效,
 * 不信;QEMU 8.2.2 实现,落地经 guest fault 诊断打点活探针复核)。 */
#include "arch/arm64/include/mm.h"       /* S2_ATTR_EMULATED */
#include "drivers/include/virtio.h"
#include "drivers/include/vuart.h"
#include "drivers/include/vgic.h"
#include "kernel/include/console.h"
#include "kernel/include/mmio.h"
#include "kernel/include/sched.h"        /* sched_console_owner */
#include "kernel/include/vcpu.h"
#include "kernel/include/vm.h"           /* vm_of_vcpu/vm_ipa_to_pa/VM_DEV_* */

/* ESR_ISS(data abort from lower EL, EC=0x24;ARM DDI 0487):
 *   [24] ISV(寄存器现场有效) [23:22] SAS(访问宽) [21:16] SRT(寄存器号)
 *   [6] WnR(1=写) [5:0] DFSC */
void mmio_dispatch(struct vcpu *v, u64 esr) __attribute__((noreturn));

void mmio_dispatch(struct vcpu *v, u64 esr)
{
    u64 iss = esr & 0x1FFFFFFUL;
    struct eshine_vm *vm = vm_of_vcpu(v);
    struct eshine_vm *own_vm = sched_console_owner ?
                               vm_of_vcpu(sched_console_owner) : NULL;
    u64 hpfar, far, ipa;
    u32 i;

    if (!((iss >> 24) & 1UL))
        guest_fault(v, esr);             /* ISV=0:无可仿真现场(默认拒绝) */
    /* IPA 重建(实测 QEMU 8.2.2):HPFAR_EL2 存 (IPA>>12)<<4,页基=hpfar<<8;
     * HPFAR 只有页粒度,页内偏移取 FAR[11:0](纯 stage-2 下 VA 即 IPA,
     * FAR 对 S2 fault 的页内偏移有效——页地址才无效,不信的是那半边) */
    __asm__ volatile("mrs %0, hpfar_el2" : "=r"(hpfar));
    __asm__ volatile("mrs %0, far_el2" : "=r"(far));
    ipa = (hpfar << 8) | (far & 0xFFFUL);

    for (i = 0; i < vm->n_dev; i++) {
        const struct vm_device *d = &vm->devices[i];

        if (d->attr != S2_ATTR_EMULATED)
            continue;
        if (ipa < d->ipa || ipa >= d->ipa + d->size)
            continue;
        /* 命中仿真窗口(第九站设备表化):按 dev 分发。
         * 关键不变量:data abort 不退休——ELR 指向 faulting 指令
         * (HVC/WFI 是"已完成"语义,ELR 指向下条;两副面孔),仿真完成
         * 必须代 retire(pc+=4),否则 ERET 原地重执行->无穷陷阱环。 */
        {
            u32 srt = (u32)((iss >> 16) & 0x1FUL);
            u32 sas = (u32)((iss >> 22) & 0x3UL);   /* SAS 编码:0/1/2/3 */
            u32 size = 1u << sas;                  /* 字节数:1/2/4/8(别把
                                                     * 编码值当字节数传——
                                                     * 半字掩码吃掉 4 字节读) */
            bool wnr = ((iss >> 6) & 1UL) != 0;

            switch (d->dev) {
            case VM_DEV_VUART:
                if (wnr)
                    vuart_mmio_write(&vm->vu, (u32)vm->vmid, ipa - d->ipa,
                                     v->gprs[srt],
                                     /* 站20 [63] 定谳:前台语义与泵的 fg 过滤
                                      * 对齐——按 VM 比,不再按 vCPU 比。双
                                      * vCPU 客的进程上下文会被内核调度到任一
                                      * vCPU(vm2 的 sh 实测漂到 vcpu1),旧比
                                      * 法把业主 VM 自家另一帧的 TX 判成后台
                                      * ——无换行的 busybox 提示符入 bg 行
                                      * 缓冲,永不再冲刷,[62] 后的 shell 交互
                                      * 全部失锚。泵侧站19D 已按 VM(sched.c
                                      * "双 vCPU 同 VM 皆前台"),此处补齐。 */
                                     own_vm == vm);
                else if (srt != 31)      /* SRT=31 -> xzr,不回填 */
                    v->gprs[srt] = vuart_mmio_read(&vm->vu, ipa - d->ipa);
                break;
            case VM_DEV_VIRTIO:
                if (wnr)
                    virtio_mmio_write(vm, (u32)vm->vmid, ipa - d->ipa,
                                      v->gprs[srt]);
                else if (srt != 31)
                    v->gprs[srt] = virtio_mmio_read(vm, (u32)vm->vmid,
                                                    ipa - d->ipa);
                break;
            case VM_DEV_VGICD:           /* 第十二站:vGIC distributor 面 */
                if (wnr)
                    vgic_dist_mmio_write(&vm->vgic, (u32)(ipa - d->ipa),
                                         v->gprs[srt], size);
                else if (srt != 31)
                    v->gprs[srt] = vgic_dist_mmio_read(&vm->vgic,
                                                        (u32)(ipa - d->ipa),
                                                        size);
                break;
            case VM_DEV_VGICR:           /* vGIC redistributor 面(站19D:
                                          * RD+SGI 帧 × nframes;帧号=off>>17,
                                          * 帧内偏移取低 17 位——每帧
                                          * RD 64KB+SGI 64KB 的树窗口契约) */
                {
                    u64 roff = ipa - d->ipa;
                    u32 frame = (u32)(roff >> 17);

                    if (wnr)
                        vgic_redist_mmio_write(&vm->vgic, frame,
                                               (u32)(roff & 0x1FFFFUL),
                                               v->gprs[srt], size);
                    else if (srt != 31)
                        v->gprs[srt] = vgic_redist_mmio_read(&vm->vgic,
                                                             frame,
                                                             (u32)(roff & 0x1FFFFUL),
                                                             size);
                }
                break;
            default:
                guest_fault(v, esr);      /* 表内未知设备:不变量破坏 */
            }
        }
        v->pc += 4UL;                    /* 代 retire:A64 定长 4 字节 */
        resume_or_preempt(v);
    }
    guest_fault(v, esr);                  /* 窗口外访存:设备层默认拒绝 */
}
