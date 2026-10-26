<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# X Thread — Station 16: a real RTOS is born on my from-scratch Type-1 hypervisor (8 posts)

**1/** Station 16 of eshine-os: VM0 just graduated from a 185-line bare-metal loop to a real task kernel — 32-priority bitmap scheduler, 100Hz tick, IRQ-driven shell. 参考RTOS's public design as the behavioral baseline, written from zero, clean-room. One day of joy, then the ghosts came out. 🧵

**2/** The core: a single unified 272-byte task frame. IRQ-preemption frames, cooperative-yield frames, and first-start frames are all the same layout — the saved ELR describes where to resume. One resume path in asm, C only picks the next task. No "special first switch" logic, ever.

**3/** Lesson #1 in kernel debugging: never mark a context switch function `noreturn`. My switcher "returns" via eret trickery — GCC, believing it never returns, scheduled loop-back garbage after the call site. The resumed task walked straight into it: NULL deref in one build, infinite prints in another. Removing the attribute fixes the epilogue. 45 minutes of QEMU -d int for this.

**4/** The real story: for 15 stations my two guest VMs ran THE SAME binary twice. Vector tables at the same addresses, same GIC usage, same stage-1 config. So "per-CPU physical registers shared across ELs need per-VM save/restore" was never exercised. The first heterogeneous pair (RTOS + bare-metal + Linux) detonated ALL of it in one day. Five register planes: VBAR, CPACR, SCTLR/TTBR0/TCR, ICH_LR0/ICH_VMCR, SIMD.

**5/** The VBAR ghost is my favorite: vm1's boot rewrote the physical vector base to its own table; vm0's next timer interrupt jumped through the wrong door — and the execution flow wandered off re-printing its own boot lines. Looked like a replay bug. It was address-space identity theft.

**6/** The GIC one was nastier: one LR0 for the whole CPU. The pump injecting vm0's timer wake would overwrite Linux's unclaimed tick — jiffies froze, boot stalled at first msleep. And an in-flight pending interrupt cannot survive a VM switch at all: HW-mapped LRs are driven by the physical line; the next VM loads its own timer and the line drops — that tick evaporates.

**7/** I also built a trap-quantum scheduler (512 MMIO traps per slice) to end Linux's masked-IRQ eternal slices — and it resurrected a 25-75% flaky boot freeze from the wake_waiter line-drop racing the save window. Bisected to a guilty verdict with controls. Then I did the right thing: moved the concurrency judge to the post-IRQ-enable window and DELETED the scheduler. Knowing how to build it is cheap; knowing when to tear it out is the lesson.

**8/** Gate: 48→55 steps, two consecutive true-green runs. `fp OK: 500000500000` — a million hand-written-asm double-adds in Q registers, closed-form exact across task switches and pump windows: the SIMD per-VM frame, paid in full. Next station: semaphores. Apache-2.0, clean-room, 参考RTOS public design as baseline, the old repo's derived tree untouched. https://github.com/eshine-os/eshine-os
