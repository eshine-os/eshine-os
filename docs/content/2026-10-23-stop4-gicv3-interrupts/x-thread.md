<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# X / Twitter thread (English, 8 tweets) — Stop 4: GICv3, real interrupts and five stacked bugs

1/ Stop 4 of my from-scratch ARM64 Type-1 hypervisor: interrupts.
The host finally lives inside its own interrupts (a 1Hz CNTHP tick),
and the guest sleeps on a virtual timer that fires in ITS vector
table, physically invisible to the host. Also: 5 stacked bugs, all
mine, all from misremembered facts. 1-2h/day, human + AI, Apache-2.0.

2/ The virt-injection chain is the payoff shot. Guest arms CNTV,
WFIs. TWI traps the WFI to EL2 (it never retires). Host sleeps too.
CNTV expires → physical INTID 27 hits EL2 → host writes ICH_LR0
(vINTID 27, HW-mapped), drops priority ONLY (no DIR). Re-enter
guest → ICV delivers → guest handles it in ITS vector table → one
virtual EOI → hardware retracts the host's physical line.

3/ Bug 1: the SGI-frame register layout. I "knew" a compact layout
from memory. Reality (QEMU gicv3_internal.h): the SGI frame mirrors
the GICD family — IGROUPR0@0x80, ISENABLER0@0x100. My group write
landed on the real enable register (stuck!), my enable write landed
in an unimplemented hole (RAZ/WI — silently dropped). Two weird
behaviors, one hallucinated offset.

4/ Bug 2: GICD_CTLR=1 enables Group 0. G1NS delivery gates on bit1
(plus ICC_IGRPEN1). A pending+enabled interrupt in the wrong group
makes the CPU interface produce NO signal at all — not IRQ, not
FIQ, nothing. Timer asserting ISTATUS=1 into the void.

5/ Bug 3: the device tree lies (by omission). I read the DTB slot
order and "deduced" which PPI was CNTHP. Wrong. Live probes settled
it: watch which pending bit correlates with timer expiry (CNTHP=26),
then watch what an armed CNTV claims — a screen-flood of
[unk-irq 0x1b] = 27. The flood itself proved level+DIR = re-pend
storm, validating the "drop priority only" design.

6/ Bug 4, the boss fight: every precondition verified GREEN —
cpuif raised the line (trace said so), CPU saw it pending (ISR=0x80),
DAIF.I clear, EL2, vectors fine — and `-d int` logged ZERO taken
exceptions, gdb breakpoints never hit. Cause: HCR_EL2 IMO is bit4,
not bit3. In QEMU's target_el_table (no EL3, rw=1 column), that one
bit decides whether an EL2 physical IRQ routes to EL2 or is
silently judged "goes to EL1" and never taken. One digit. Ticks
0→2→5.

7/ Bug 5: guest exits left the host deaf. Exception entry to EL2
auto-masks PSTATE.I, and nobody unmasked it on the way back — so
after any HVC exit the host shell ran with IRQs masked forever.
Sneaky detail: ticks kept flowing DURING guest runs because the
live PSTATE was the guest's (which had unmasked itself). A new gate
assertion ("ticks still grow after the guest exits") caught it the
same night it shipped. Fix: one `daifclr #2` in vcpu_exit.

8/ Methodology takeaway, worth the whole evening: **when you build
on an emulator, the emulator source IS the ground truth.** I diffed
my QEMU 8.2.2 against the upstream v8.2.2 tag file by file, and
every "remembered" bit position got checked against source or a
live probe before touching the keyboard. Five bugs, one lesson.
Gate is now 20 steps, twice green. Next stop: scheduling —
VCPU_WAIT is already the seed. github.com/eshine-os/eshine-os
