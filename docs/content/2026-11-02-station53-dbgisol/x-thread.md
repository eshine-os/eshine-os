# X 8 推(站 53 debug/PMU 隔离)

1/8
eshine-os station 53: closing the "both trees diseased" finding from the reference-audit audit — debug/PMU registers were physically shared across VMs. One hypervisor, one set of hardware breakpoints, every guest sees every other guest's debug state. Fix: MDCR_EL2.TDA. #hypervisor

2/8
Mechanism: TDA traps ALL EL1 external-debug register accesses to EL2. Handler: MDSCR_EL1 emulated per-VM (new frame slot), breakpoint/watchpoint/PMU families RAZ/WI (read zero, write drop). Linux's hw_breakpoint probe sees "no debug hardware" and degrades gracefully.

3/8
Landmine #1: QEMU TCG doesn't enforce MDCR_EL2 traps AT ALL. Instrumented handler: zero firings. PMCR read back a real ID value. So the mechanism can't be exercised on QEMU...

4/8
...but plot twist: QEMU's debug registers are inert stubs anyway (MDSCR write reads back 0). The platform has NO leak surface by construction. So: mechanism stays in the code (real-hw ready), platform stubs cover QEMU, and a probe reports both shapes honestly.

5/8
Landmine #2 — the one that cost blood: I took TPM=bit0/TPMCR=bit1 from the reference implementation's sysregs.h. MDCR_EL2=0x203. Result: the third child process of the service governor hung SILENTLY. Bisected to that single MSR.

6/8
Root cause: bits [4:0] of MDCR_EL2 are HPMN (Hypervisor PMU Number). Bit0-1 aren't TPM's home — they're HPMN's. Writing 0x203 set HPMN=3 and QEMU died quietly somewhere in PMU logic. The reference implementation's header was wrong.

7/8
Lesson engraved: a reference implementation is also written by humans. When a bit definition touches a reserved/multi-bit field boundary (HPMN[4:0]), re-verify. Two authorities colliding means one of them is a typo.

8/8
Final: MDCR_GUEST=0x200 (TDA only, matching the ARM ARM), dbgdemo probe (trapped/quiescent dual-shape platform report), vmdbg slot dump, vm1 boot probe. Gates 151→154, smoke 14/14. #ARM #virtualization
