/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#ifndef ESHINE_VM_H
#define ESHINE_VM_H

#include <stdbool.h>
#include "arch/arm64/include/mm.h"     /* mem_region/vm_device/vm_space */
#include "kernel/include/types.h"
#include "kernel/include/vcpu.h"

#define ESHINE_MAX_VMS    2
#define ESHINE_MAX_VCPUS  1           /* 本站每 VM 1 vCPU */
#define ESHINE_MAX_MEM    4
#define ESHINE_MAX_DEV    4

enum vm_state {
    VM_INIT,
    VM_CREATED,
    VM_RUNNING,
    VM_PAUSED,
    VM_TEARDOWN,
    VM_DEAD,
    VM_STATE_MAX,
};

/* 实体内嵌(静态全量,spec §2):槽位生命周期即 VM 生命周期。
 * 分配器落地后只需把内嵌数组换成指针,builder 与命令层不动。 */
struct eshine_vm {
    enum vm_state state;
    u64 vmid;                          /* VTTBR.VMID(8-bit) */
    u32 n_mem, n_dev;
    struct vcpu       vcpus[ESHINE_MAX_VCPUS];
    struct mem_region mem[ESHINE_MAX_MEM];
    struct vm_device  devices[ESHINE_MAX_DEV];
    struct vm_space   space;
};

const char *vm_state_name(enum vm_state s);
bool vm_transition(struct eshine_vm *vm, enum vm_state next);
void vm_state_demo(void);

/* 生命周期 API(命令层):用户错误礼貌拒绝(打消息返回 -1),
 * vm_transition 内部表外转换仍 panic(默认拒绝核心不变)。 */
int  vm_create(u32 id);
int  vm_start(u32 id);           /* 异步启动:置 RUNNING 立即回 */
struct vcpu *vm_vcpu(u32 id);         /* id -> vcpu(console owner 用) */
int  vm_kill(u32 id);
void vm_list(void);

#endif /* ESHINE_VM_H */
