/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#ifndef ESHINE_VM_H
#define ESHINE_VM_H

#include <stdbool.h>
#include "kernel/include/types.h"

#define ESHINE_MAX_VCPUS 8

/* Forward declarations — 实体类型后续阶段引入 */
struct vcpu;
struct mem_region;
struct vm_device;

enum vm_state {
    VM_INIT,
    VM_CREATED,
    VM_RUNNING,
    VM_PAUSED,
    VM_TEARDOWN,
    VM_DEAD,
    VM_STATE_MAX,
};

struct eshine_vm {
    enum vm_state state;
    struct vcpu        *vcpus[ESHINE_MAX_VCPUS];
    struct mem_region  *mem[ESHINE_MAX_VCPUS];
    struct vm_device   *devices[ESHINE_MAX_VCPUS];
};

const char *vm_state_name(enum vm_state s);
bool vm_transition(struct eshine_vm *vm, enum vm_state next);
void vm_state_demo(void);

#endif /* ESHINE_VM_H */
