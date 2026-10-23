/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
/* VM 抽象:静态双槽 + 配置驱动;表驱动状态机,表外 panic(默认拒绝) */
#include "arch/arm64/include/mm.h"
#include "kernel/include/console.h"
#include "kernel/include/panic.h"
#include "kernel/include/util.h"
#include "kernel/include/vcpu.h"
#include "drivers/include/gicv3.h"
#include "kernel/include/sched.h"
#include "kernel/include/vm.h"

static struct eshine_vm vm_table[ESHINE_MAX_VMS];

/* 预定义配置:RAM 窗口不重叠 —— VM0 0x50000000/16MB, VM1 0x51000000/16MB,
 * 同 IPA 0x40000000 不同 PA,隔离由映射层保证(物理事实,非约定)。 */
#define VM_RAM_IPA    0x40000000UL
#define VM_RAM_SIZE   0x1000000UL     /* 16MB = 8 个 2MB block */
#define VM_UART_IPA   0x09000000UL
#define VM_UART_PA    0x09000000UL
#define VM_UART_SIZE  0x1000UL

static const u64 vm_ram_pa[ESHINE_MAX_VMS] = { 0x50000000UL, 0x51000000UL };

static void vm_id_label(u32 id)
{
    console_puts("vm");
    util_putdec(id);
    console_puts(": ");
}

static const char *names[VM_STATE_MAX] = {
    [VM_INIT]      = "VM_INIT",
    [VM_CREATED]   = "VM_CREATED",
    [VM_RUNNING]   = "VM_RUNNING",
    [VM_PAUSED]    = "VM_PAUSED",
    [VM_TEARDOWN]  = "VM_TEARDOWN",
    [VM_DEAD]      = "VM_DEAD",
};

/* allowed[current][next] — 唯一合法转换集。
 * 第二站新增两条:CREATED→TEARDOWN(vmkill),DEAD→INIT(槽位复用)。 */
static const bool allowed[VM_STATE_MAX][VM_STATE_MAX] = {
    [VM_INIT]     = { [VM_CREATED]  = true, },
    [VM_CREATED]  = { [VM_RUNNING]  = true, [VM_TEARDOWN] = true, },
    [VM_RUNNING]  = { [VM_PAUSED]   = true, [VM_TEARDOWN] = true, },
    [VM_PAUSED]   = { [VM_RUNNING]  = true, [VM_TEARDOWN] = true, },
    [VM_TEARDOWN] = { [VM_DEAD]     = true, },
    [VM_DEAD]     = { [VM_INIT]     = true, },
};

const char *vm_state_name(enum vm_state s)
{
    if (s >= VM_STATE_MAX)
        return "VM_?";
    return names[s];
}

bool vm_transition(struct eshine_vm *vm, enum vm_state next)
{
    if (next >= VM_STATE_MAX || !allowed[vm->state][next])
        panic("vm_transition: illegal state transition");
    vm->state = next;
    return true;
}

int vm_create(u32 id)
{
    struct eshine_vm *vm;

    if (id >= ESHINE_MAX_VMS) {
        console_puts("no such vm\n");
        return -1;
    }
    vm = &vm_table[id];
    if (vm->state == VM_DEAD)
        vm_transition(vm, VM_INIT);            /* DEAD→INIT 槽位复用 */
    if (vm->state != VM_INIT) {
        vm_id_label(id);
        console_puts("denied: slot busy\n");
        return -1;
    }

    vm->vmid = id;
    vm->n_mem = 1;
    vm->mem[0].ipa  = VM_RAM_IPA;
    vm->mem[0].pa   = vm_ram_pa[id];
    vm->mem[0].size = VM_RAM_SIZE;
    vm->mem[0].attr = S2_ATTR_NORMAL;
    vm->n_dev = 1;
    vm->devices[0].ipa  = VM_UART_IPA;
    vm->devices[0].pa   = VM_UART_PA;
    vm->devices[0].size = VM_UART_SIZE;
    vm->devices[0].attr = S2_ATTR_DEVICE;
    vm->vcpus[0].pc = VM_RAM_IPA;              /* 帧即真相:新跑=初始化帧 */
    vm->vcpus[0].sp = 0x40200000UL;            /* guest 自会重设 */
    vm->vcpus[0].pstate = 0x3C5UL;            /* EL1h + DAIF 全掩 */
    memset(vm->vcpus[0].gprs, 0, sizeof(vm->vcpus[0].gprs));
    vm->vcpus[0].exit_reason = VCPU_EXIT;

    if (stage2_build(&vm->space, vm->vmid, vm->mem, vm->n_mem,
                     vm->devices, vm->n_dev) != 0) {
        vm_id_label(id);
        console_puts("stage2_build rejected config\n");
        return -1;
    }
    vm_transition(vm, VM_CREATED);
    vm_id_label(id);
    console_puts("CREATED (ram ");
    console_puthex(vm->mem[0].pa);
    console_puts(", entry ");
    console_puthex(vm->vcpus[0].pc);
    console_puts(")\n");
    return 0;
}

int vm_run(u32 id)
{
    struct eshine_vm *vm;
    u64 reason;
    bool resumed;

    if (id >= ESHINE_MAX_VMS) {
        console_puts("no such vm\n");
        return -1;
    }
    vm = &vm_table[id];
    resumed = (vm->state == VM_PAUSED);
    if (vm->state == VM_PAUSED || vm->state == VM_CREATED)
        vm_transition(vm, VM_RUNNING);       /* 断点续跑 / 新跑 */
    else {
        vm_id_label(id);
        console_puts("denied: vmcreate first\n");
        return -1;
    }
    vm_id_label(id);
    if (resumed)
        console_puts("resumed (breakpoint)\n");
    else
        console_puts("entering EL1 via stage-2 (non-identity map)\n");
    do {
        vm->vcpus[0].slice_left = SCHED_QUANTUM_TICKS;   /* 过渡期:泵化后由泵置 */
        sched_need_resched = 0;
        reason = vcpu_run(&vm->vcpus[0], vm->space.vttbr);
        if (reason == VCPU_WAIT) {
            __asm__ volatile("wfi");   /* 宿主陪睡:滴答/vtimer 到点唤醒 */
            vm->vcpus[0].pc += 4;      /* WFI 陷阱不 retire:代其"完成",guest
                                          * 醒后自查唤醒条件(vtimer 标志) */
            if (vm->vcpus[0].wait_state == 0UL)     /* 仅首次进等待置 WAIT:
                                                     * wfi 循环反复醒睡不得抹掉
                                                     * 27 已置的 WOKE */
                vm->vcpus[0].wait_state = 1UL;
            if (vm->vcpus[0].wait_state == 2UL) {   /* WOKE:泵化前的过渡内联,
                                                     * Task 2 由泵收编注入点 */
                vgic_inject_vtimer();
                vm->vcpus[0].wait_state = 0UL;
            }
        }
        /* PREEMPT:量子耗尽被出口切走;阻塞语义下继续跑自己,
         * 调度泵接手后此分支消失(让位给下一 VM)。 */
    } while (reason == VCPU_WAIT || reason == VCPU_PREEMPT);
    if (reason == VCPU_YIELD) {
        vm_transition(vm, VM_PAUSED);
        vm_id_label(id);
        console_puts("yielded -> PAUSED (vmrun to resume)\n");
        return 0;
    }
    vm_transition(vm, VM_TEARDOWN);
    vm_transition(vm, VM_DEAD);
    vm_id_label(id);
    if (reason == VCPU_FAULT)
        console_puts("faulted -> VM_DEAD\n");
    else
        console_puts("VM_DEAD\n");
    return 0;
}

int vm_kill(u32 id)
{
    struct eshine_vm *vm;

    if (id >= ESHINE_MAX_VMS) {
        console_puts("no such vm\n");
        return -1;
    }
    vm = &vm_table[id];
    if (vm->state != VM_CREATED && vm->state != VM_PAUSED) {
        vm_id_label(id);
        console_puts("kill denied (CREATED/PAUSED only, no async kill)\n");
        return -1;
    }
    vm_transition(vm, VM_TEARDOWN);
    vm_transition(vm, VM_DEAD);
    vm_id_label(id);
    console_puts("killed before run\n");
    return 0;
}

void vm_list(void)
{
    u32 i;

    console_puts("VM table:\n");
    for (i = 0; i < ESHINE_MAX_VMS; i++) {
        struct eshine_vm *vm = &vm_table[i];

        console_puts("  [");
        util_putdec(i);
        console_puts("] ");
        console_puts(vm_state_name(vm->state));
        if (vm->state != VM_INIT) {
            console_puts("  ram ");
            console_puthex(vm->mem[0].pa);
            console_puts("  entry ");
            console_puthex(vm->vcpus[0].pc);
        }
        console_putc('\n');
    }
}

/* 演示对象独立于 vm_table;实体内嵌 vm_space 后体积 ~20KB,必须静态。
 * 每次演示前直置 INIT(演示自复位,非生命周期通道)。 */
void vm_state_demo(void)
{
    static struct eshine_vm demo;
    static const enum vm_state path[] = {
        VM_CREATED, VM_RUNNING, VM_PAUSED, VM_RUNNING, VM_TEARDOWN, VM_DEAD,
    };
    u32 i;

    demo.state = VM_INIT;
    console_puts("VM state machine (lifecycle first):\n");
    console_puts("  VM_INIT -> VM_CREATED -> VM_RUNNING <-> VM_PAUSED\n");
    console_puts("  VM_RUNNING/VM_PAUSED -> VM_TEARDOWN -> VM_DEAD\n");
    console_puts("  any other transition: panic (default-deny)\n");

    console_puts("demo walk:");
    for (i = 0; i < sizeof(path) / sizeof(path[0]); i++) {
        vm_transition(&demo, path[i]);
        console_puts(" ");
        console_puts(vm_state_name(demo.state));
    }
    console_puts("\n");
}
