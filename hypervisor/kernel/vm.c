/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
/* VM 抽象:静态双槽 + 配置驱动;表驱动状态机,表外 panic(默认拒绝) */
#include "arch/arm64/include/mm.h"
#include "kernel/include/console.h"
#include "kernel/include/dtb.h"
#include "kernel/include/panic.h"
#include "kernel/include/util.h"
#include "kernel/include/vcpu.h"
#include "drivers/include/gicv3.h"
#include "drivers/include/timer.h"
#include "kernel/include/sched.h"
#include "kernel/include/vm.h"

static struct eshine_vm vm_table[ESHINE_MAX_VMS];

/* 预定义配置(第十四站几何表化):RAM 窗口不重叠 —— VM0 0x50000000/16MB,
 * VM1 0x51000000/16MB,VM2(Linux)0x58000000/128MB(恰占满 QEMU 512MB 顶,
 * 与 0/1 槽有安全距离);同 IPA 0x40000000 不同 PA,隔离由映射层保证
 * (物理事实,非约定)。入口与 x0(DTB 指针,Linux 入口协议)随槽几何化。 */
#define VM_RAM_IPA    0x40000000UL
#define VM_UART_IPA   0x09000000UL
#define VM_UART_PA    0x09000000UL
#define VM_UART_SIZE  0x1000UL
#define VM_DTB_IPA    0x40F00000UL    /* guest 视角 DTB 指针(= RAM_IPA + 0xF00000) */
#define VM_DTB_OFF    0xF00000UL      /* 第十一站:固定偏移,bootloader 同构 */
#define VM_DTB_BUDGET 0x10000UL        /* 64KB 预算 */

struct vm_geometry {
    u64 ram_pa;
    u64 ram_size;
    u64 entry;                     /* IPA 入口:裸机=RAM 基,Linux=+0x80000(text_offset) */
    const unsigned char *dtb;      /* per-slot 树(第十四站:裸机/Linux 两树) */
    const unsigned char *dtb_end;
};

static const struct vm_geometry vm_geo[ESHINE_MAX_VMS] = {
    [0] = { 0x50000000UL, 0x1000000UL, 0x40000000UL,
            eshine_dtb, eshine_dtb_end },
    [1] = { 0x51000000UL, 0x1000000UL, 0x40000000UL,
            eshine_dtb, eshine_dtb_end },
    [2] = { 0x58000000UL, 0x8000000UL, 0x40080000UL,   /* Linux:128MB,Image@+0x80000 */
            eshine_linux_dtb, eshine_linux_dtb_end },
};

/* 站十八D:VM→pCPU 静态绑定(不做迁移/均衡,YAGNI 红线):
 * vm0/vm1 归 CPU0(与宿主 shell 同核),vm2 归 CPU1 专泵。 */
static const u32 vm_cpu[ESHINE_MAX_VMS] = {0, 0, 1};


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
    vm->mem[0].pa   = vm_geo[id].ram_pa;
    vm->mem[0].size = vm_geo[id].ram_size;
    vm->mem[0].attr = S2_ATTR_NORMAL;
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

    /* DTB 注入(第十一站):整树拷进 guest RAM 固定偏移,bootloader 同构。
     * 头校验(magic+totalsize==实测)+预算检查,坏 blob 默认拒绝 create;
     * 双 VM 共树(IPA 硬件一致),EL2 恒等映射直写 PA 即可。 */
    {
        const unsigned char *blob = vm_geo[id].dtb;
        u64 dtb_size = (u64)(vm_geo[id].dtb_end - blob);
        u32 be32_0 = ((u32)blob[0] << 24) | ((u32)blob[1] << 16) |
                     ((u32)blob[2] << 8) | (u32)blob[3];
        u32 be32_4 = ((u32)blob[4] << 24) | ((u32)blob[5] << 16) |
                     ((u32)blob[6] << 8) | (u32)blob[7];
        if (be32_0 != 0xd00dfeedUL || be32_4 != dtb_size ||
            dtb_size > VM_DTB_BUDGET) {
            vm_id_label(id);
            console_puts("dtb rejected (bad blob)\n");
            return -1;
        }
        memcpy((void *)(vm_geo[id].ram_pa + VM_DTB_OFF), blob, dtb_size);
    }
    vuart_init(&vm->vu);
    memset(&vm->virtio, 0, sizeof(vm->virtio));  /* regs 复位;磁盘数据不在生命周期内 */
    vgic_reset(&vm->vgic);                /* 第十二站:distributor 随 VM 生命周期 */

    /* vGIC 窗口(第十二站):镜像 QEMU virt 真布局,树里 intc 节点据此描述。
     * ESHINE_MAX_DEV=4 恰好满编:vuart/virtio/vgicd/vgicr。 */
    vm->devices[2].ipa  = 0x08000000UL;
    vm->devices[2].pa   = 0;
    vm->devices[2].size = 0x10000UL;
    vm->devices[2].attr = S2_ATTR_EMULATED;
    vm->devices[2].dev  = VM_DEV_VGICD;
    vm->devices[3].ipa  = 0x080A0000UL;
    vm->devices[3].pa   = 0;
    vm->devices[3].size = 0x20000UL;     /* RD 帧 + SGI 帧(单 vCPU 一帧) */
    vm->devices[3].attr = S2_ATTR_EMULATED;
    vm->devices[3].dev  = VM_DEV_VGICR;
    vm->n_dev = 4;                     /* 四设备满编 */
    vm->vcpus[0].pc = vm_geo[id].entry;        /* 帧即真相:新跑=初始化帧 */
    vm->vcpus[0].sp = 0x40200000UL;            /* guest 自会重设 */
    vm->vcpus[0].pstate = 0x3C5UL;            /* EL1h + DAIF 全掩 */
    memset(vm->vcpus[0].gprs, 0, sizeof(vm->vcpus[0].gprs));
    vm->vcpus[0].gprs[0] = VM_DTB_IPA;         /* x0=DTB 指针(bootloader 义务;
                                                 * 裸机 guest 不读,Linux 入口协议) */
    vm->vcpus[0].exit_reason = VCPU_EXIT;
    /* 帧尾复位(第五-八站字段):DEAD->INIT 槽位复用时不许带出旧值
     * (wait_state 残 1 则新客永不被泵,潜在雷实测前修) */
    vm->vcpus[0].wait_state = 0UL;
    vm->vcpus[0].wait_reason = 0UL;
    vm->vcpus[0].vtimer_ctl = 0UL;
    vm->vcpus[0].vtimer_cval = 0UL;
    vm->vcpus[0].sp_el0 = 0UL;        /* 站十四:guest current 槽清零起步
                                       * (Linux primary 早期 init_cpu_task
                                       * 自会写入 init_task) */

    if (stage2_build(&vm->space, vm->vmid, vm->mem, vm->n_mem,
                     vm->devices, vm->n_dev) != 0) {
        vm_id_label(id);
        console_puts("stage2_build rejected config\n");
        return -1;
    }
    if (id == 2)
        sched_poke_cpu1();          /* 站十八D:CPU1 泵即扫新客 */
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
    if (!resumed) {
        /* VM 出生即清 stage-1(第十四站从 vcpu_run 收编至此:进场不许
         * 碰 guest 的 SCTLR_EL1,出生清一次防上一位 guest 的陈旧页表
         * 泄给新客;Linux 随后自会置 M 走两级翻译)。 */
        u64 sctlr;

        __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
        sctlr &= ~1UL;
        __asm__ volatile("msr sctlr_el1, %0" :: "r"(sctlr));
    }
    if (id == 2)
        sched_poke_cpu1();          /* 站十八D:CPU1 泵即扫 */
    vm_id_label(id);
    if (resumed)
        console_puts("resumed (breakpoint)\n");
    else
        console_puts("entering EL1 via stage-2 (non-identity map)\n");
    return 0;
}

/* 退场善后:yield 暂停/死亡,两者都回收 RX 所有权(guest 不再读线)。 */
/* 站十八D:VM 状态锁——kill(CPU0)与在途退场(CPU1 的 vm_finish)
 * 对同一 VM 的迁移都是"查后写",跨核非原子窗互踩即表外 panic。锁内
 * 重检 RUNNING 者独占入殓/让出;他侧见已入殓只收 owner。锁外打印。 */
static volatile unsigned long vm_state_lock;

static void vm_state_lock_take(void)
{
    for (;;) {
        unsigned long v;

        __asm__ volatile("ldaxr %0, [%1]" : "=r"(v) : "r"(&vm_state_lock));
        if (!v) {
            unsigned long ok;

            __asm__ volatile("stxr %w0, %w2, [%1]"
                             : "=&r"(ok)
                             : "r"(&vm_state_lock), "r"(1UL));
            if (!ok)
                return;
        }
        __asm__ volatile("wfe");
    }
}

static void vm_state_lock_drop(void)
{
    __asm__ volatile("stlr %0, [%1]" :: "r"(0UL), "r"(&vm_state_lock));
    __asm__ volatile("sev");
}

static void vm_finish(u32 id, u64 reason)
{
    struct eshine_vm *vm = &vm_table[id];
    bool paused = false, dead = false;

    vuart_tx_flush(&vm->vu, id);        /* 死/停前冲尾:半行不吞(tag 定前缀) */
    vm_state_lock_take();
    if (vm->state != VM_RUNNING) {
        vm_state_lock_drop();
        /* 已入殓/已让出(他侧先手):只收 owner,不二次迁移 */
        if (sched_console_owner == &vm->vcpus[0])
            sched_console_owner = NULL;
        return;
    }
    if (reason == VCPU_YIELD) {
        vm->state = VM_PAUSED;          /* 锁内自证迁移,表语义构造成立 */
        paused = true;
    } else {
        vm->state = VM_TEARDOWN;
        vm->state = VM_DEAD;
        dead = true;
    }
    vm_state_lock_drop();
    if (paused) {
        vm_id_label(id);
        console_puts("yielded -> PAUSED (vmrun to resume)\n");
    } else if (dead) {
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
u32 sched_vtimer_sweep(void)
{
    u64 now;
    u32 id, woke = 0;

    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(now));
    for (id = 0; id < ESHINE_MAX_VMS; id++) {
        struct vcpu *v = &vm_table[id].vcpus[0];

        if (vm_cpu[id] != cpu_id())
            continue;                /* 站十八D:各核只扫自家客的帧 */
        if (vm_table[id].state != VM_RUNNING)
            continue;
        if (v->wait_state != 1UL || v->wait_reason != VCPU_WAIT_TIMER)
            continue;
        if (!(v->vtimer_ctl & 1UL) || v->vtimer_cval > now)
            continue;
        v->wait_state = 2UL;              /* WOKE:泵轮到时注 LR */
        v->vtimer_ctl = 0UL;              /* ctx 熄火:防切入装回过期值即重挂 */
        woke++;
    }
    return woke;
}

/* 调度泵:轮转游标遍历,每 VM 一个量子;宿主权威在 shell(命令即原子段,
 * 泵只在 shell 等输入处被调)。前台模式(owner 非空)只跑 owner VM——
 * console 独占,双 guest 并行仅在后台模式(owner=NULL)。 */
/* 单 VM 跑一轮:就绪/等待过滤+WOKE 与设备注入+共享面装载+切出保存
 * +原因善后。CPU0 泵(前台过滤在泵侧)与 CPU1 专泵共用;前台过滤
 * 不在函数内——vm2 在 CPU1 无 RX 直读竞争(owner 环推送),免过滤。 */
static bool pump_run_vm(u32 id)
{
    struct eshine_vm *vm = &vm_table[id];
    struct vcpu *v = &vm->vcpus[0];
    u64 reason;

    if (vm->state != VM_RUNNING)
    return false;                    /* 非就绪 */
    if (v->wait_state == 1UL) {
        /* 设备中断等待者(RX/virtio 完成等非 TIMER 源)仍有待取
         * 事件(注入与消费的时序缝隙的兜底网):再唤醒,本轮流到
         * 它时注入+消费;TIMER 睡眠与无事件者继续让位。
         * 第十二站:再唤醒同律过门控(guest 使能了才算"待取") */
        if (v->wait_reason != VCPU_WAIT_TIMER &&
            ((vuart_rx_irq_ready(&vm->vu) &&
              vgic_spi_enabled(&vm->vgic, GIC_SPI_UART)) ||
             (virtio_irq_ready(vm) &&
              vgic_spi_enabled(&vm->vgic, GIC_SPI_VIRTIO))))
            v->wait_state = 0UL;
        else
            return false;
    }
    sched_vtimer_last[cpu_id()] = id;   /* 站十八A/D:物理 CNTV 归属随本核进场 */        /* 站十八A:物理 CNTV 归属随进场 */
    v->slice_left = SCHED_QUANTUM_TICKS;
    /* 站十六:GIC 虚拟面恢复必须在 WOKE 注入之前(注入覆写成新唤醒;
     * 放后面会吃掉刚注入的 v27,睡眠永不醒,实测)。LR0 是 per-pCPU
     * 单点:双 vtimer 客并跑,泵为 vm0 注入会覆写 vm2 未认领的 tick。 */
    __asm__ volatile("msr ich_lr0_el2, %0" :: "r"(v->lr_vtimer));
    __asm__ volatile("msr ich_vmcr_el2, %0" :: "r"(v->vmcr));
    sched_need_resched = 0;
    if (v->wait_state == 2UL) {        /* WOKE 专属 vtimer(27/扫描经
                                        * ctx 表定位;RX 走直通 runnable
                                        * 不经 WOKE——两源零串台) */
        if (v->wait_reason == VCPU_WAIT_TIMER) {
            /* 第十二站:注入过门控——guest 没使能 PPI 27 就不注入,
             * 也不代完成(它自己的配置自己负责;隔离面哲学) */
            if (vgic_ppi_enabled(&vm->vgic, GIC_PPI_VTIMER)) {
                vgic_inject_vtimer(vgic_ppi_prio(&vm->vgic,
                                                 GIC_PPI_VTIMER));/* per-pCPU 单 LR 只能泵挂 */
            }
        }
        v->wait_state = 0UL;
        v->wait_reason = 0UL;
    }
    /* 注入门控(第十二站):distributor 成为权威——guest 使能了
     * 对应 SPI 才注 LR。通道不变:vtimer 27(HW)/vuart 33(LR1)/
     * virtio 35(LR3)。 */
    if (vuart_rx_irq_ready(&vm->vu) &&
        vgic_spi_enabled(&vm->vgic, GIC_SPI_UART)) {
        /* 第八站:UART RX 虚拟中断,LR1 通道泵注入(同纪律:防串台)。
         * RIS 在 DR 读时重评定:多字符爆发每注一发弹一个,续注排空 */
        vgic_inject_uart();
    }
    if (virtio_irq_ready(vm) &&
        vgic_spi_enabled(&vm->vgic, GIC_SPI_VIRTIO)) {
        /* 第十站:virtio 完成中断(LR3) */
        vgic_inject_virtio();
    }
    /* vtimer ctx 切入对(第七站):cortex-a57(v8.0)无 CNTV 陷阱位,
     * guest 直写物理;泵以切换对齐——切入还本人 ctx,物理 CNTV
     * 恒为"当前客"的;切出即存(全出口含 YIELD/EXIT/FAULT/WAIT),
     * 别家错过由每拍扫描兜住。CNTVOFF 共享 0:双 guest 同墙钟。 */
    /* 站十六入口序:共享面先装,vtimer 最后装并直接进场——装载与
     * vcpu_run 之间零宿主代码,27 无从插队。共享面(全 EL 一份的
     * 物理面,异构双客互换必串,各客归各)=SIMD 帧 + VBAR/CPACR +
     * stage-1 控制面(SCTLR/TTBR0/TCR);GIC 虚拟面已在 WOKE 注入
     * 前装(见上)。 */
    /* 入口段同屏(站十六):装载->eret 之间的 27 若插队,已装载的
     * ctl 会被 wake_waiter 关线清零,guest 带着软件以为 armed、硬件
     * 实熄的定时器跑,msleep 永不到点,tickless 停摆(实测残余闪断
     * ~25% 即此窗)。eret 装 SPSR 自然解屏。 */
    __asm__ volatile("msr daifset, #2");
    /* 站十八D:VMPIDR——EL1 的 MPIDR 视角即 VMPIDR_EL2。三客都是
     * 单核视角(DTB cpu@0),vm2 跑在 CPU1 上真 mpidr=1,Linux 的
     * 重分发帧发现按 TYPER 亲和匹配会"no re-distributor"炸靴
     * (实测);虚拟化回 cpu0,与树/仿真面全一致。宿主读 MPIDR_EL2
     * 不受影响。 */
    __asm__ volatile("msr vmpidr_el2, %0" :: "r"(0x80000000UL));
    fpsimd_load(v);
    __asm__ volatile("msr vbar_el1, %0" :: "r"(v->vbar));
    __asm__ volatile("msr cpacr_el1, %0" :: "r"(v->cpacr));
    __asm__ volatile("msr tcr_el1, %0" :: "r"(v->tcr));
    __asm__ volatile("msr ttbr0_el1, %0" :: "r"(v->ttbr0));
    __asm__ volatile("msr sctlr_el1, %0" :: "r"(v->sctlr));
    __asm__ volatile("msr cntv_cval_el0, %0" :: "r"(v->vtimer_cval));
    __asm__ volatile("msr cntv_ctl_el0, %0" :: "r"(v->vtimer_ctl));
    reason = vcpu_run(v, vm->space.vttbr);

    /* 站十六出口序:DAIF 先屏,vtimer 最先存。vcpu_exit 已解 IRQ,
     * 这段保存窗若插进 el2_irq_host 的 27(vgic_wake_waiter 写
     * cntv_ctl=0"关线"),armed 状态被存成 0 -> WAIT 归 RX -> 永久
     * 停摆。旧窗(仅 tick 检查)近乎为零;本站共享面保存把窗撑到
     * 微秒级,轮转宿主相高频期(陷阱量子 ~40ms 一片)命中率陡升
     * ——单跑 Linux 实测闪断,T1/T2 侥幸即此窗。整段屏蔽 DAIF。 */
    __asm__ volatile("msr daifset, #2");
    __asm__ volatile("mrs %0, cntv_ctl_el0" : "=r"(v->vtimer_ctl));
    __asm__ volatile("mrs %0, cntv_cval_el0" : "=r"(v->vtimer_cval));
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(v->sctlr));
    __asm__ volatile("mrs %0, ttbr0_el1" : "=r"(v->ttbr0));
    __asm__ volatile("mrs %0, tcr_el1" : "=r"(v->tcr));
    __asm__ volatile("mrs %0, vbar_el1" : "=r"(v->vbar));
    __asm__ volatile("mrs %0, cpacr_el1" : "=r"(v->cpacr));
    __asm__ volatile("mrs %0, ich_lr0_el2" : "=r"(v->lr_vtimer));
    __asm__ volatile("mrs %0, ich_vmcr_el2" : "=r"(v->vmcr));
    fpsimd_save(v);
    __asm__ volatile("msr daifclr, #2");
    cur_vcpu = NULL;                /* 宿主语境:shell 命令期 tick 不误
                                      * 计入 guest 量子(27 分支已独立查) */
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
        /* 站十五撤自动归还:真 Linux 壳恒 250Hz armed,timer 睡
         * 即被收台,输入永远喂给宿主 shell(owner 形同虚设)。
         * owner 从此是显式接管,归还只走 vmconsole host / 逃生
         * 0x1d / VM 死亡三口(裸机客另有 host HVC#4 自报)。 */
        (void)0;
    } else {
        vm_finish(id, reason);        /* YIELD/EXIT/FAULT */
    }
    return true;
}

bool sched_pump(void)
{
    static u32 rr_next;
    bool progressed = false;
    u32 n;

    /* 站十八A:在途回业主——27 三分支置的标志,本圈以最后进场者为
     * 旋转起点(首圈即回业主消费自己的拍;重装帧 ctl=1+过期 cval,
     * 线仍挂,HW LR 进场重锁 pending,guest 消费即闭环)。业主已
     * 死/停则顺手熄线收尸;他核业主与本核无关(数组 per-CPU)。 */
    if (sched_vtimer_recall[cpu_id()]) {
        sched_vtimer_recall[cpu_id()] = 0UL;
        if (vm_table[sched_vtimer_last[cpu_id()]].state != VM_RUNNING ||
            vm_cpu[sched_vtimer_last[cpu_id()]] != cpu_id())
            vgic_wake_waiter();
        else
            rr_next = (u32)sched_vtimer_last[cpu_id()];
    }
    for (n = 0; n < ESHINE_MAX_VMS; n++) {
        u32 id = (rr_next + n) % ESHINE_MAX_VMS;
        struct eshine_vm *vm = &vm_table[id];
        struct vcpu *v = &vm->vcpus[0];

        if (vm_cpu[id] != cpu_id())
            continue;                     /* 站十八D:vm2 归 CPU1 专泵 */
        if (sched_console_owner && sched_console_owner != v)
            continue;                     /* 前台模式:console 独占,免 RX 竞争 */
        if (pump_run_vm(id)) {
            rr_next = (id + 1) % ESHINE_MAX_VMS;
            progressed = true;
        }
    }
    return progressed;
}

/* 站十八D:CPU1 专泵——vm2 单 VM,无量子无轮转(PPI26 不配),跑到
 * 出(WAIT/EXIT/FAULT);空转 WFE:本核 27(睡客醒)与 SEV 邮箱
 * (RX 推送/vmrun)皆可唤醒。noreturn:secondary_main 直落于此。 */
void pump_secondary(void)
{
    for (;;) {
        (void)pump_run_vm(2);
        __asm__ volatile("wfe");
    }
}

int vm_kill(u32 id)
{
    struct eshine_vm *vm;
    bool already;

    if (id >= ESHINE_MAX_VMS) {
        console_puts("no such vm\n");
        return -1;
    }
    vm = &vm_table[id];
    /* 站十四:RUNNING 也入列——宿主对僵尸客的绝对处决权。ARM 单核
     * 无 EL2 级不可屏蔽抢占(DAIF 关尽的死循环客把 PPI26 也挡掉,
     * Linux panic 后实测 shell 冻结),管理面处决是唯一解;无异步
     * 问题:shell 命令即原子段,执行时泵必不在跑,帧为静态槽位。
     * 死客(INIT/DEAD)拒绝:无事可做,防误报。 */
    if (vm->state != VM_CREATED && vm->state != VM_PAUSED &&
        vm->state != VM_RUNNING) {
        vm_id_label(id);
        console_puts("kill denied (dead or never created)\n");
        return -1;
    }
    if (sched_console_owner == &vm->vcpus[0]) {
        sched_console_owner = NULL;         /* 管理面接管输入路由 */
        console_puts("console -> host\n");
    }
    vm_state_lock_take();
    already = !(vm->state == VM_CREATED || vm->state == VM_PAUSED ||
                vm->state == VM_RUNNING);
    if (!already) {
        vm->state = VM_TEARDOWN;   /* 站十八D:锁内自证迁移(kill 与
                                     * CPU1 在途退场赛跑,查后写互踩会
                                     * 表外 panic;谁先见 RUNNING 谁入殓) */
        vm->state = VM_DEAD;
    }
    vm_state_lock_drop();
    if (already) {
        vm_id_label(id);
        console_puts("kill denied (already closing)\n");
        return -1;
    }
    if (id == 2)
        sched_poke_cpu1();          /* 站十八D:即扫即停 */
    vm_id_label(id);
    console_puts("killed (host authority)\n");
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
