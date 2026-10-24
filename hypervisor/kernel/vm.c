/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
/* VM 抽象:静态双槽 + 配置驱动;表驱动状态机,表外 panic(默认拒绝) */
#include "arch/arm64/include/mm.h"
#include "kernel/include/console.h"
#include "kernel/include/panic.h"
#include "kernel/include/util.h"
#include "kernel/include/vcpu.h"
#include "drivers/include/gicv3.h"
#include "drivers/include/timer.h"
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
    vm->n_dev = 2;
    vm->devices[0].ipa  = VM_UART_IPA;
    vm->devices[0].pa   = 0;                /* 仿真设备无 PA:不映射,只有窗口 */
    vm->devices[0].size = VM_UART_SIZE;
    vm->devices[0].attr = S2_ATTR_EMULATED; /* 撤直通:访问即 data abort 陷阱 */
    vm->devices[0].dev  = VM_DEV_VUART;
    vm->devices[1].ipa  = VIRTIO_BASE;     /* 第十站:virtio-mmio 窗口(承接 vblk 槽位) */
    vm->devices[1].pa   = 0;
    vm->devices[1].size = 0x1000UL;
    vm->devices[1].attr = S2_ATTR_EMULATED;
    vm->devices[1].dev  = VM_DEV_VIRTIO;
    vuart_init(&vm->vu);
    memset(&vm->virtio, 0, sizeof(vm->virtio));  /* regs 复位;磁盘数据不在生命周期内 */
    vm->vcpus[0].pc = VM_RAM_IPA;              /* 帧即真相:新跑=初始化帧 */
    vm->vcpus[0].sp = 0x40200000UL;            /* guest 自会重设 */
    vm->vcpus[0].pstate = 0x3C5UL;            /* EL1h + DAIF 全掩 */
    memset(vm->vcpus[0].gprs, 0, sizeof(vm->vcpus[0].gprs));
    vm->vcpus[0].exit_reason = VCPU_EXIT;
    /* 帧尾复位(第五-八站字段):DEAD->INIT 槽位复用时不许带出旧值
     * (wait_state 残 1 则新客永不被泵,潜在雷实测前修) */
    vm->vcpus[0].wait_state = 0UL;
    vm->vcpus[0].wait_reason = 0UL;
    vm->vcpus[0].vtimer_ctl = 0UL;
    vm->vcpus[0].vtimer_cval = 0UL;

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

/* 异步启动:置 RUNNING 立即回 shell,跑动交给调度泵(shell 等输入处)。 */
int vm_start(u32 id)
{
    struct eshine_vm *vm;
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
    return 0;
}

/* 退场善后:yield 暂停/死亡,两者都回收 RX 所有权(guest 不再读线)。 */
static void vm_finish(u32 id, u64 reason)
{
    struct eshine_vm *vm = &vm_table[id];

    vuart_tx_flush(&vm->vu, id);        /* 死/停前冲尾:半行不吞(tag 定前缀) */
    if (reason == VCPU_YIELD) {
        vm_transition(vm, VM_PAUSED);
        vm_id_label(id);
        console_puts("yielded -> PAUSED (vmrun to resume)\n");
    } else {
        vm_transition(vm, VM_TEARDOWN);
        vm_transition(vm, VM_DEAD);
        vm_id_label(id);
        if (reason == VCPU_FAULT)
            console_puts("faulted -> VM_DEAD\n");
        else
            console_puts("VM_DEAD\n");
    }
    sched_console_owner = NULL;   /* guest 不再读线,RX 归还宿主 */
}

struct vcpu *vm_vcpu(u32 id)
{
    return &vm_table[id].vcpus[0];
}

/* vcpu -> 所属 VM(mmio_dispatch 用;2 槽全表扫,表外=内部不变量破坏) */
struct eshine_vm *vm_of_vcpu(struct vcpu *v)
{
    u32 id;

    for (id = 0; id < ESHINE_MAX_VMS; id++)
        if (&vm_table[id].vcpus[0] == v)
            return &vm_table[id];
    panic("vm_of_vcpu: vcpu not in vm_table");
}

/* 静态翻译(第九站建/第十站 virtio 数据面):IPA 落 mem[] 区且全尺寸含 → PA,
 * 否则 0(调用方按设备错处理,默认拒绝不杀 guest)。
 * EL2 恒等映射,PA 即宿主可直接访存的地址。 */
u64 vm_ipa_to_pa(struct eshine_vm *vm, u64 ipa, u64 size)
{
    u32 i;

    for (i = 0; i < vm->n_mem; i++) {
        const struct mem_region *r = &vm->mem[i];

        if (ipa >= r->ipa && ipa + size <= r->ipa + r->size)
            return r->pa + (ipa - r->ipa);
    }
    return 0UL;
}

/* 每拍到期扫描(第七站,多等待者):物理 CNTV 恒为"当前客"的,别家
 * 到期不响物理线——本扫描按保存的 ctx 表判定,WOKE 过期者(多家皆醒,
 * 先后由各自 cval 决定)。纯表逻辑:无 27 未响则无需关线降优先级
 * (vgic_wake_waiter 只属真正响过的物理 27 路径);拍频 = 1 tick,
 * 与泵粒度同阶——WOKE 的生效本就要等下一次泵轮到它,与即时判定
 * 等效(单核调度的本质粒度)。 */
void sched_vtimer_sweep(void)
{
    u64 now;
    u32 id;

    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(now));
    for (id = 0; id < ESHINE_MAX_VMS; id++) {
        struct vcpu *v = &vm_table[id].vcpus[0];

        if (vm_table[id].state != VM_RUNNING)
            continue;
        if (v->wait_state != 1UL || v->wait_reason != VCPU_WAIT_TIMER)
            continue;
        if (!(v->vtimer_ctl & 1UL) || v->vtimer_cval > now)
            continue;
        v->wait_state = 2UL;              /* WOKE:泵轮到时注 LR */
        v->vtimer_ctl = 0UL;              /* ctx 熄火:防切入装回过期值即重挂 */
    }
}

/* 调度泵:轮转游标遍历,每 VM 一个量子;宿主权威在 shell(命令即原子段,
 * 泵只在 shell 等输入处被调)。前台模式(owner 非空)只跑 owner VM——
 * console 独占,双 guest 并行仅在后台模式(owner=NULL)。 */
bool sched_pump(void)
{
    static u32 rr_next;
    bool progressed = false;
    u32 n;

    for (n = 0; n < ESHINE_MAX_VMS; n++) {
        u32 id = (rr_next + n) % ESHINE_MAX_VMS;
        struct eshine_vm *vm = &vm_table[id];
        struct vcpu *v = &vm->vcpus[0];
        u64 reason;

        if (vm->state != VM_RUNNING)
            continue;                     /* 非就绪 */
        if (v->wait_state == 1UL) {
            /* 设备中断等待者(RX/virtio 完成等非 TIMER 源)仍有待取
             * 事件(注入与消费的时序缝隙的兜底网):再唤醒,本轮流到
             * 它时注入+消费;TIMER 睡眠与无事件者继续让位 */
            if (v->wait_reason != VCPU_WAIT_TIMER &&
                (vuart_rx_irq_ready(&vm->vu) || virtio_irq_ready(vm)))
                v->wait_state = 0UL;
            else
                continue;
        }
        if (sched_console_owner && sched_console_owner != v)
            continue;                     /* 前台模式:console 独占,免 RX 竞争 */
        rr_next = (id + 1) % ESHINE_MAX_VMS;
        v->slice_left = SCHED_QUANTUM_TICKS;
        sched_need_resched = 0;
        if (v->wait_state == 2UL) {        /* WOKE 专属 vtimer(27/扫描经
                                            * ctx 表定位;RX 走直通 runnable
                                            * 不经 WOKE——两源零串台) */
            if (v->wait_reason == VCPU_WAIT_TIMER)
                vgic_inject_vtimer();       /* per-pCPU 单 LR 只能泵挂 */
            v->wait_state = 0UL;
            v->wait_reason = 0UL;
        }
        if (vuart_rx_irq_ready(&vm->vu)) {  /* 第八站:UART RX 虚拟中断,
                                             * LR1 通道泵注入(同纪律:防串台)。
                                             * RIS 在 DR 读时重评定:多字符
                                             * 爆发每注一发弹一个,续注排空 */
            vgic_inject_uart();
        }
        if (virtio_irq_ready(vm)) {        /* 第十站:virtio 完成中断(LR3) */
            vgic_inject_virtio();
        }
        /* vtimer ctx 切入对(第七站):cortex-a57(v8.0)无 CNTV 陷阱位,
         * guest 直写物理;泵以切换对齐——切入还本人 ctx,物理 CNTV
         * 恒为"当前客"的;切出即存(全出口含 YIELD/EXIT/FAULT/WAIT),
         * 别家错过由每拍扫描兜住。CNTVOFF 共享 0:双 guest 同墙钟。 */
        __asm__ volatile("msr cntv_cval_el0, %0" :: "r"(v->vtimer_cval));
        __asm__ volatile("msr cntv_ctl_el0, %0" :: "r"(v->vtimer_ctl));
        reason = vcpu_run(v, vm->space.vttbr);
        __asm__ volatile("mrs %0, cntv_ctl_el0" : "=r"(v->vtimer_ctl));
        __asm__ volatile("mrs %0, cntv_cval_el0" : "=r"(v->vtimer_cval));
        cur_vcpu = NULL;                /* 宿主语境:shell 命令期 tick 不误
                                          * 计入 guest 量子(27 分支已独立查) */
        progressed = true;
        if (reason == VCPU_PREEMPT) {
            sched_preempts++;             /* 量子耗尽:出口已转写帧,下一 VM */
        } else if (reason == VCPU_WAIT) {
            v->pc += 4;                   /* WFI 不 retire:代完成,醒后自查 */
            v->wait_state = 1UL;
            /* 两源等待(第六站;第七站起判据用切出保存的本人 ctx——
             * 物理此刻可能是他人视角):cntv 武装 = TIMER(到期经 WOKE
             * 注 LR 醒),否则 RX(shell 输入入环直通唤醒)。RX 等待持
             * owner——输入即唤醒源,前台的意义所在;TIMER 等待才归还
             * (宿主睡期间要能敲命令,回收出口在 guest 的 host 命令)。 */
            v->wait_reason = (v->vtimer_ctl & 1UL) ? VCPU_WAIT_TIMER
                                                   : VCPU_WAIT_RX;
            if (sched_console_owner == v && v->wait_reason == VCPU_WAIT_TIMER)
                sched_console_owner = NULL;
        } else {
            vm_finish(id, reason);        /* YIELD/EXIT/FAULT */
        }
    }
    return progressed;
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
