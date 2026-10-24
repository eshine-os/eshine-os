<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# X / Twitter thread (EN, 8 tweets) — Stop 12: vGIC Distributor/Redistributor

**1/8**
Stop 12 of my from-scratch ARM64 Type-1 hypervisor: the guest's
interrupts finally get a DISTRIBUTOR. Emulated GICD + GICR (mirroring
QEMU virt's real layout), guest does a real-OS-style GIC bring-up, and
the host's injection pump is now GATED on the guest's enable bits.
The distributor becomes the authority. 🧵👇

**2/8**
The semantic shift is the station: before, the host saw an event and
wrote a list register — the host decided. Now the pump checks the
guest's emulated GICD/GICR state first: vtimer PPI 27 needs
GICR_ISENABLER0 bit27, uart SPI 33 & virtio SPI 35 need
GICD_ISENABLER0 bits 1/3. Not enabled → no injection, no proxy-WFI
completion. The host EXECUTES the guest's decisions instead of making
them.

**3/8**
Guest boot now reads like a real OS: probe PIDR2 ((x & 0xf0) == 0x30
→ "you're GICv3"), enable CTLR, clear WAKER ProcessorSleep, enable
INTIDs via ISENABLER writes, then READ-BACK VERIFY everything before
reporting "gicd v3 ok / gic on: 27 33 35". ~10 traps, full round
trip. And stop 11's missing intc node returns to the device tree —
the tree doesn't lie about the future, but promises get cashed.

**4/8**
Battle 1: my stage-2 validator rejected the new windows — every
emulated device window had to be exactly ONE 4K page. But real GIC
layout is inherently multi-page (GICD 64KB, GICR RD+SGI frames
128KB). For EMULATED windows no descriptor is ever built — the window
size is just the trap range. Relaxed: multi-page allowed for
emulated, direct-mapped stays single-page.

**5/8**
Battle 2 — my favorite kind: first version decoded GICD's 0x080-0x3FF
as ONE register family sharing (off-0x080)>>2 indexing. ISENABLER0
(0x100) computed as n=32 — out of bounds: writes silently dropped,
reads RAZ. The guest's read-back verification caught it IMMEDIATELY:
"gic FAIL: ppi27". The verification step I designed caught the bug I
wrote hours earlier. Criteria ARE tests.

**6/8**
Battle 3, the sneaky one: MMIO dispatch passed the ISS syndrome's SAS
FIELD VALUE to the model — SAS is an ENCODING (0=1B,1=2B,2=4B,3=8B).
I treated 2 as "2 bytes": 4-byte reads masked to halfword, 0x08000000
read back as 0. Paper-traced the path three times, all "correct".
Two lines of probe prints nailed it. Self-verified correct ≠
observed correct. Fix: size = 1 << sas.

**7/8**
Bonus oops for the road: my new file's SPDX line ended "-->"
(markdown muscle memory from doc writing) — block comment never
closed, swallowed the copyright line. -Wcomment caught it in one
shot. Write too much documentation and your hands start crossing
over into C files.

**8/8**
Offsets verified against the ARM manuals (station-8 discipline:
bit numbers from the spec, not from "code that happens to run" —
GICR_TYPER's Last is bit 4). Register emulation layered: probe/
enable/wake paths have real semantics (RWP always 0 — Linux's
polling loops pass with zero wait), state regs store-and-readback,
unknown = RAZ/WI. Gate: 36 steps ×2.
github.com/eshine-os/eshine-os · www.eshine-os.org

## 附图清单

- T1: gicd v3 ok / gic on: 27 33 35 self-report (hook)
- T2: gating semantics before/after diagram (封面图)
- T4: GICD register family decode table (0x80-wide families)
- T6: probe output: W off=0x10100 val=0x08000000 / R ret=0
- T8: gate 36 PASS ×2
