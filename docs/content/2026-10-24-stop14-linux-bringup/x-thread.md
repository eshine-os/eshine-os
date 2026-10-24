<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# X/Twitter thread — Station 14: Linux boots on our from-scratch Type-1 hypervisor

1/ The moment we've been building toward for 14 stations: a real Linux 6.6
kernel now runs as a VM on eshine-os, our clean-room ARM64 Type-1
hypervisor. It boots in 1.06s of kernel time and panics exactly where a
realist expects: no rootfs. That panic is our station's finish line. 🧵

2/ Linux recognized our honest device tree ("Machine model: eshine-os,vm"),
brought up our emulated GICv3, attached our emulated PL011 as ttyAMA0, and
registered our virtio block device as vda — then tried to mount it as root.
Every station's device work (DTB injection, vGIC, PL011, virtio) held under
a real kernel's scrutiny.

3/ Root cause #1 — the silent 6.7M/sec exception storm. Linux stalled with
NO output, NO oops, NO host crash. QEMU's -d int revealed a self-trap
loop in enter_from_kernel_mode: it reads `current`... which arm64 Linux
keeps in SP_EL0. And SP_EL0 is shared across ALL exception levels.

4/ We ran our host C world on SP_EL0 (SPSel=0). Every ERET handed Linux a
host stack pointer as its `current` — the first dereference faulted, and
the fault handler read `current` again. Infinite vector self-trap, silent
because printk itself needs `current`. Bare-metal guests never touch
SP_EL0; 13 stations of green hid it. Fix: host world → SP_EL2, sp_el0
added to the trap frame (save/restore per vCPU — standard hypervisor
discipline).

5/ Root cause #2 — jiffies frozen at INITIAL. The whole boot ran on
sched_clock with ZERO ticks consumed. Linux's arch_timer_handler_virt is a
POLLING handler: it reads CNTV ISTATUS before claiming the event. Our
injection path "helpfully" disabled the virtual timer (anti-storm habit
from bare-metal days) → handler reads ISTATUS=0 → returns IRQ_NONE → the
tick evaporates, no re-arm, timekeeping dies.

6/ Fix: for a RUNNING guest, never touch CNTV — its handler masks and
re-arms by itself. "Turn off the line" is only for a SLEEPING guest
(nobody else will). Two paths, two policies. Also: inject with the
guest's own configured priority (a hard 0x00 lands in Linux's pseudo-NMI
threshold and takes the tick through the NMI path).

7/ Root cause #3 — a 4-byte offset. Linux programs SPIs via GICD_ISENABLER1
(0x104, spec word1). Our station-12 decode had a homegrown convention
treating word0 (0x100) as "the SPI word" and silently dropped word1. Our
own guest shared the convention; Linux didn't. gdb on the live machine:
completion pending, SPI enable word = 0. Decode aligned to IHI0069, guest
offsets +4. Lesson: don't invent standards.

8/ Bonus: a kernel panic is local_irq_disable + a spin loop, and PSTATE.I
masks even EL2-routed IRQs — no hardware preemption can break in (single
pCPU ARM). The only answer is management-plane authority: vmkill now
reclaims RUNNING VMs. 45-step gate, double-green. Next stop: a rootfs,
and /sbin/init takes the baton.
