# X / Twitter thread (English, 8 tweets) — Stop 1: stage-2 + first guest

1/ Night one of the 27.04 series on my from-scratch ARM64 Type-1
hypervisor: stage-2 translation, the first guest address space, and a
single uninitialized register that spent the whole night pretending
to be three different bugs. All on GitHub, Apache-2.0, built 1-2h/day
with AI pair programming.

2/ The deliverable: a stage-2 module (VTCR + table/block/page
descriptors), a vCPU entry/exit path, and a bare-metal guest that
thinks it owns physical 0x40000000 — while the hypervisor quietly
relocates it to 0x50000000. Non-identity mapping: if translation
works, virtualization works.

3/ Two IO paths for one UART, on purpose: the banner goes through
HVC hypercalls (EL2 prints for the guest), the interactive shell
touches the device page directly through stage-2 (nGnRE + XN).
Both green or it doesn't count.

4/ Warmup bugs: a missing AF bit in the descriptors (ESR 0x8200000A,
instruction abort — the answer was one table away in the ARM ARM),
and a guest pointer living in x1 across an HVC because the inline
asm didn't declare the clobber. An ABI is a contract; the compiler
only reads what you declare, not what you meant.

5/ The main course. Symptoms: guest banner prints PERFECTLY — 41
hypercalls, every character right. The exit HVC arrives. Four debug
prints pass. Then the host returns into garbage and dies with
ESR 0x02000000. Every static check said "fine."

6/ So: gdb-multiarch + QEMU -s -S, single-step through. The truth in
four lines:
A: host sp = 0x4020ce60 (normal)
B: after `msr spsel, #0` → sp = 0  ← SP_EL0 was NEVER initialized
C: host frame = {0x20, 0} (read from the QEMU bootloader zero page)
H: after `ret x17` → pc = 0 (executing the bootloader)

7/ Root cause: QEMU's direct EL2 boot resets SPSel to 1 — my whole
host world had been running on SP_EL2 since the first instruction,
and SP_EL0 was 0. My "excp stack dance" then overwrote the LIVE
host stack and switched to the dead bank. Banner survived because
ERET doesn't need the stack; the exit path died because the "host
frame" was zeros. One bug saved half the system and killed the
other half — that's how it hid.

8/ Fix: three lines. _start normalizes the bank (SPSel=0, host on
SP_EL0, SP_EL2 dedicated to exception entry). Moral: register bank
defaults have no standard answer — boot code must normalize, never
assume. Gate: 8 true-green checks × 8 consecutive runs. Next stop:
VM abstraction. github.com/eshine-os/eshine-os
