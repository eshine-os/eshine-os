<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# X / Twitter thread (EN, 8 tweets) — Stop 13: PL011 Completion

**1/8**
Stop 13 of my from-scratch ARM64 Type-1 hypervisor: the virtual UART
grows an AMBA identity card. Linux's AMBA bus doesn't bind drivers by
vibes — the DEVICE identifies itself through read-only ID registers.
No PERIPHID, no driver. This stop completes the last Linux-boot
prerequisite (after DTB and GICD/GICR). 🧵👇

**2/8**
The ID: PL011's PERIPHID bytes (0xFE0-0xFEC) spell 0x11/0x10/0x14/0x00
→ AMBA id 0x00141011; the PCELLID bytes (0xFF0-0xFFC) spell
0x0D/0xF0/0x05/0xB1 → 0xB105F00D. Our guest now reads all eight bytes
at boot, verifies, and self-reports: "pl011 on: amba 00141011".
The bus checks your ID card before your acting skills.

**3/8**
Also added: the control register family (CR/IBRD/FBRD/LCR_H/IFLS/DMACR
store-and-readback, reset values matching the real chip: CR=0x300 =
TXE|RXE set, UARTEN clear), and the guest init sequence now follows
Linux's pl011 driver step for step: shut down (CR=0) → baud (115200
@24MHz) → 8N1+FIFO → enable (CR=0x301).

**4/8**
Baud isn't emulated — no clock source, no timing. Registering dividers
without simulating time is honest; faking a clock would not be.

**5/8**
The one battle: I made CR semantics REAL — DR writes require
UARTEN|TXE, RX interrupts require UARTEN|RXE. A disabled UART drops
your writes, exactly like hardware. First test: EVERY pre-enable
output vanished. The pl011 self-report... AND the stop-12 gicd/gic
self-reports — all silently eaten by the gate.

**6/8**
Not a bug — real semantics BITING MY OWN BOOT ORDER for the first
time. The old model's "writes ignored" leniency had bred a
"print-first, init-later" habit. Real chips don't spoil anyone:
disabled means no output. Ever.

**7/8**
The fix doesn't remove the gate (that would be regression to
leniency). It puts the boot order back into real-OS shape: pl011
comes FIRST (probe silently, self-report AFTER enabling), gic after —
exactly Linux's law: a driver finishes bringing up the device before
there's a console to talk about it. Even FAIL diagnostics moved
after enable: error reports need an output channel too.

**8/8**
Regression-as-acceptance: all 36 previous gate criteria passed
unchanged through the new CR gates — echo, 100-MMIO selftest,
virtio completion, vtimer sleep. Gate: 37 steps ×2. Linux campaign:
DTB ✓ GICD ✓ PL011 ✓ — bring-up is next.
github.com/eshine-os/eshine-os · www.eshine-os.org

## 附图清单

- T1: pl011 on: amba 00141011 self-report (hook)
- T2: ID bytes table (封面图)
- T5: swallowed-output vs after-enable boot comparison
- T8: gate 37 PASS ×2 + prerequisites checklist
