<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# X / Twitter thread (EN, 8 tweets) — Stop 6: Device Emulation

**1/8**
Stop 6 on my from-scratch ARM64 Type-1 hypervisor (1-2h/evening with AI):
trap-based device emulation. The guest's UART driver — untouched, still
speaking plain PL011 — now writes to an *emulated* device. The real one
was quietly pulled out from under it. That's the whole magic of device
virtualization: same contract, new counterparty. 🧵👇

**2/8**
Mechanism: remove the UART from the stage-2 page tables → every access
is a translation fault → data abort to EL2 → decode ESR (which register,
read/write, width) → rebuild the IPA from HPFAR → match against the
emulated window (unknown address = default-deny, VM dies) → answer from
a per-VM vuart model → write the value back into the guest's registers.

**3/8**
First invariant that bit me: HVC and WFI traps retire — ELR points AFTER
the instruction. A synchronous data abort does NOT retire: ELR points AT
the faulting instruction. Emulate the effect, then ERET without
advancing PC, and the guest re-executes the same load. Forever. My -d int
log showed ONE ldr trapped 13,748 times in a loop. Every iteration
"correct"; the loop itself wrong.

**4/8**
Second: the frame-vs-live trap. Hypervisor writes the emulated value
into the saved trap frame (gprs[1] = 0x10, verified). The live guest
register still shows the old pointer. A watchpoint on the frame slot
exposed it: the eret tail — "load guest x0 last, x0 is still the frame
pointer" — used x1 as scratch, pumping gprs[0] into x1 every eret.

**5/8**
That tail was written in stop 3 and harmless for three stations: HVCs
only ever return values in x0. MMIO emulation returns into ANY register
(SRT), and the very first SRT=1 access — the guest's FR read — detonated
it. Lesson: register-shuffling in asm entry code is part of the frame
contract. Bills arrive late.

**6/8**
Third: the HPFAR formula. I got it wrong twice (trust all of FAR: no —
stage-2 FAR's page address is invalid; trust only HPFAR: no — page
granular, loses the page offset, my FR reads all landed on the DR
offset). Probe with the guest's fault command settled it: QEMU's
HPFAR = IPA>>8; page base = HPFAR<<8, page offset = FAR[11:0].

**7/8**
Also retired: HVC #0 (putc) and #3 (getc) — hypercalls are control-only
now; all I/O rides the emulated device. The selftest upgraded from
"registers survive 100 HVCs" to "registers survive 100 MMIO traps".
Background guest output prints line-buffered with a [vmN] prefix —
interleaved VM output stays readable. Gate: 24 steps, double green.

**8/8**
The arc: stop 5 replaced the HVC getc storm with FR polling + WFI.
Next stop replaces the polling itself — with a virtual SPI 33 injected
through the GIC's list registers. Every generation of waiting mechanism
gets replaced by the next. github.com/eshine-os/eshine-os ·
www.eshine-os.org

## 附图清单

- T1: `[vm0] eshine-guest VM0 @EL1 (vuart @trap)` 输出特写
- T3: -d int 连环陷阱截图(13,748)
- T4/T5: gdb 帧 vs 活体对照(封面图)
- T6: HPFAR=0x300000 探针行
- T7: make verify 24 步 PASS
