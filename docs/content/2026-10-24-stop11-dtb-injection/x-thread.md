<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# X / Twitter thread (EN, 8 tweets) — Stop 11: DTB Injection

**1/8**
Stop 11 of my from-scratch ARM64 Type-1 hypervisor: the guest's worldview
moves out of hardcoded constants into a real device tree — injected by the
host at VM create. The hardware starts to describe itself. First
foundation stone for booting Linux one day. 🧵👇

**2/8**
The tree isn't hand-written. QEMU's virt machine dumps its own DTB:
`qemu-system-aarch64 -M virt,dumpdtb=` → decompile → trim. 1MB of real
description becomes 1057 bytes of honest description. Kept nodes are
byte-identical to QEMU's originals (trim, don't rewrite); two patches
with receipts: memory 512MB→16MB (that's what we actually give), the
virtio slot's IRQ → our real number (SPI 3 = INTID 35).

**3/8**
Nice surprise during trimming: the tree's interrupt specifiers — SPI 1,
SPI 3, PPI 11 — map EXACTLY to the channels my earlier stations picked
(INTID 33 uart, 35 virtio, 27 vtimer). Those numbers were always aligned
with QEMU's real hardware; the tree just makes the convention explicit.

**4/8**
The most valuable decision was what NOT to include: **no GIC node.**
We haven't emulated GICD/GICR yet (guest interrupts flow via GIC list
registers directly). Writing an intc node into the tree would advertise
a service that doesn't exist. psci: same. A device tree must not lie
about the future.

**5/8**
Delivery: vm_create memcpy's the blob into guest RAM at a fixed offset
(0x40F00000, last 1MB of the 16MB window) — same semantics as a real
bootloader, zero new hypercalls. Header validation (magic + totalsize)
+ 64KB budget; a bad blob rejects the create. Both VMs share the tree
(their IPA hardware is identical).

**6/8**
Guest side: a ~120-line read-only FDT walker — byte cursor over the
big-endian u32 token stream, path-depth matching, compatible-string
readout. The cursor is bounded by size_dt_struct: a corrupt blob may
not spin the guest through quanta.

**7/8**
The one battle this stop was mine: 0x40000000+0xF00000 is 0x4**0F**00000.
I wrote 0x4F000000 — one hex digit misplaced. The guest's first tree
read data-aborts; the fault machinery prints the full crime scene
(FAR/HPFAR/pc/x3); the VM dies CLEANLY. Host and the other VM: untouched.
Ten stations of isolation infrastructure, validated by my own typo.

**8/8**
Also closed out last stop's "chimera" incident (help walkthrough header
said 31, body said 25 — silent replace failures). New discipline, first
execution: gate script + shell help + ARCHITECTURE must update in the
same commit, renumbering descending + assert-verified, help rewritten
whole-block. Gate: 34 steps ×2. github.com/eshine-os/eshine-os ·
www.eshine-os.org

## 附图清单

- T1: three "fdt ok" self-check lines (hook)
- T2: 1MB → 1057B trim comparison (封面图)
- T4: the trimmed tree source with "no GIC node" annotation
- T7: fault printout vs host-still-running split screen
- T8: gate 34 PASS ×2
