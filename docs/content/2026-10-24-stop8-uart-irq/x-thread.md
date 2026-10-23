<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# X / Twitter thread (EN, 8 tweets) — Stop 8: UART IRQ Injection

**1/8**
Stop 8 of my from-scratch ARM64 Type-1 hypervisor: the guest's serial
input goes interrupt-driven — a purely virtual SPI 33 delivered through
the GIC's list registers. Polling retires. This stop's debugging story
is my favorite kind of trap: readback correct, effect absent. 🧵👇

**2/8**
Setup: vuart (stop 6's emulated UART) gains an interrupt face — IMSC/
RIS/MIS/ICR, modeled after real PL011 semantics. Host pushes input into
the per-VM ring, sets RIS; when RXIM is enabled, the pump injects
vSPI 33 into LR1 (vtimer owns LR0) before entering the guest.

**3/8**
Silent failure: the LR reads back EXACTLY as written. ICH_HCR enabled.
VMCR sane. Yet the vIRQ line never rises — the guest walks right up to
WFI with a "pending" LR and never vectors. Worse: QEMU's WFI treats
even an INVALID LR as "pending", completes without trapping — the guest
spins through quanta burning the CPU while the pump re-injects forever.

**4/8**
The truth, straight from the ARM ARM: ICH_LR is bit60=Group, bit61=HW,
bit62=State(pending). My stop-4 constants said PENDING=1<<61, HW=1<<62 —
WRONG names. The vtimer injection set all three bits, accidentally the
correct form, and ran for four stations. My UART copy set Group+bit61
(=HW!) with the state bit EMPTY = invalid LR. Readback doesn't validate.

**5/8**
Follow-up lesson from the trial-and-error: adding the "HW" bit (actually
the state bit) + pINTID makes it a HW-mapped LR — whose pending state
FOLLOWS THE PHYSICAL LINE. Physical SPI 33 has no source. Never
delivers. Pure-virtual SPI wants Group+State, NO HW bit.

**6/8**
Second battle: the guest died on its FIRST MMIO instruction — ESR ISV=0,
no register syndrome. GCC had merged the IMSC write into
`str w1, [x18], #-32` — a post-index writeback store; the syndrome
can't encode writeback, the host can't know the written value, and
correctly rejects. Fix: all guest MMIO through volatile asm accessors —
formally guaranteeing plain ldr/str. Compiler cleverness is the
emulator's enemy.

**7/8**
Third: a 6-char burst echoed only 5 chars — one pop per injection, then
the guest WFI'd with the rest still in the host ring. Fix A: drain-all
ISR (read DR until empty — like every real UART driver). Fix B: the
pump re-wakes RX-waiters that still have irq-ready data. Both, because
races.

**8/8**
The arc closes: stop 5's HVC getc-storm → stop 6's FR-polling → stop 8's
virtual interrupt. Every generation of waiting mechanism gets replaced
by the next, and each one buries its predecessor. Gate: 28 steps ×2.
github.com/eshine-os/eshine-os · www.eshine-os.org

## 附图清单

- T1: `[vm0] uart irq on` 输出特写
- T3/T4: 回读探针行 + ARM ARM 位号对照(封面图)
- T6: post-index 反汇编截图
- T8: make verify 28 步 PASS
