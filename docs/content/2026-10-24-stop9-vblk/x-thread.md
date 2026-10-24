<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# X / Twitter thread (EN, 8 tweets) — Stop 9: Block Device (vblk)

**1/8**
Stop 9 of my from-scratch ARM64 Type-1 hypervisor: the guest grows
STORAGE. A RAM disk with a doorbell interface, async completion
interrupt, and persistence across VM lifecycles. Zero debugging this
stop — first-test-all-green — and the reason why is the best part. 🧵👇

**2/8**
The interface philosophy that's worth more than the code itself:
"Memory is the transport, registers are the doorbell." The 512B data
buffer lives in the GUEST'S OWN RAM — it's the sole writer, no trust
issue. The host translates IPA→PA through the mem[] map and directly
accesses guest physical memory for the first time (EL2 identity map).

**3/8**
MMIO carries only 5 words: CMD/ADDR/STATUS/CAP/IMSC. Bulk data over
memory, control over traps. No per-word trap copying. This slice IS
the virtqueue seed — next stop grows it into a real virtio device.

**4/8**
Sync service, async completion. RAM has no real latency, so the data
move completes inside the doorbell trap; only the completion interrupt
is async: pending bit → pump injects vSPI 34 → ERET raises the virtual
line → guest ISR reads STATUS, clears pending, reports "blk wrote".
Request → service → completion → ISR: the full device lifecycle in
one minimal loop.

**5/8**
The persistence question: how do I guarantee data survives
"destroy VM → recreate VM"? I wrote ZERO persistence code. The disk
is a static array in vblk.c — it's simply NOT on the vm_create reset
path. Persistence is a STRUCTURAL guarantee, not a code path.
Things placed correctly don't need maintenance.

**6/8**
Second device lands, and a debt matures instantly: single-device
lookup was fine with ONE device. Now vm_device gets a `dev` field and
dispatches VM_DEV_VUART/VM_DEV_VBLK; unknown type in table = invariant
broken = fault. Default-deny reaches the device layer. LRs now hold
vtimer(HW)/vuart/vblk — three of a57's four slots.

**7/8**
Now the payoff story. Five test chains (enable/write/read/destroy/
recreate-persist) passed ON THE FIRST RUN. Not because this stop was
easy — because stops 6 & 8 already paid full price: MMIO dispatch,
the ELR two-faces lesson, SRT writeback, LR bit-form, asm accessors,
device-wait semantics. Every minute spent on skepticism back then
became dividend now.

**8/8**
The compounding has started: skepticism's interest gets paid out in
device stations. Gate 28 → 31 steps ×2 (three block-device criteria).
Next stop: the seed grows into virtio proper — same disk, industry
standard protocol. github.com/eshine-os/eshine-os · www.eshine-os.org

## 附图清单

- T1: dwrite/dread → destroy → recreate → dread sequence (hook)
- T2: guest RAM vs MMIO split diagram (封面图)
- T5: static array vs vm_create reset path annotation
- T7: 28→31 gate PASS ×2
