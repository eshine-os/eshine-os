/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
/* MMIO 分发:stage-2 未映射区访存陷阱 -> 仿真设备模型。
 * HPFAR_EL2 是 stage-2 fault 的 IPA 权威来源(FAR_EL2 对 S2 fault 无效,
 * 不信;QEMU 8.2.2 实现,落地经 guest fault 诊断打点活探针复核)。 */
#include "arch/arm64/include/mm.h"       /* S2_ATTR_EMULATED */
#include "drivers/include/vuart.h"
#include "kernel/include/console.h"
#include "kernel/include/mmio.h"
#include "kernel/include/sched.h"        /* sched_console_owner */
#include "kernel/include/vcpu.h"
#include "kernel/include/vm.h"           /* vm_of_vcpu */

/* ESR_ISS(data abort from lower EL, EC=0x24;ARM DDI 0487):
 *   [24] ISV(寄存器现场有效) [23:22] SAS(访问宽) [21:16] SRT(寄存器号)
 *   [6] WnR(1=写) [5:0] DFSC */
void mmio_dispatch(struct vcpu *v, u64 esr) __attribute__((noreturn));
void mmio_dispatch(struct vcpu *v, u64 esr)
{
    u64 iss = esr & 0x1FFFFFFUL;
    struct eshine_vm *vm = vm_of_vcpu(v);
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
        /* 命中仿真窗口:vuart(当前唯一仿真设备;多设备后此处查设备表)。
         * 关键不变量:data abort 不退休——ELR 指向 faulting 指令
         * (HVC/WFI 是"已完成"语义,ELR 指向下条;两副面孔),仿真完成
         * 必须代 retire(pc+=4),否则 ERET 原地重执行->无穷陷阱环。 */
        if ((iss >> 6) & 1UL) {          /* WnR:写 */
            vuart_mmio_write(&vm->vu, (u32)vm->vmid, ipa - d->ipa,
                             v->gprs[(iss >> 16) & 0x1FUL],
                             v == sched_console_owner);
        } else {
            if (((iss >> 16) & 0x1FUL) != 31)   /* SRT=31 -> xzr,不回填 */
                v->gprs[(iss >> 16) & 0x1FUL] =
                    vuart_mmio_read(&vm->vu, ipa - d->ipa);
        }
        v->pc += 4UL;                    /* 代 retire:A64 定长 4 字节 */
        resume_or_preempt(v);
    }
    guest_fault(v, esr);                  /* 窗口外访存:设备层默认拒绝 */
}
