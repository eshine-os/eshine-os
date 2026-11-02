# X 8 推(站 56 DFX 收口,批次 B 完)

1/8
eshine-os station 56: batch B finale. Three small closures: a query face for the event trail, a per-VM preempt ledger, and three "both trees diseased" audit verdicts. #buildinpublic #hypervisor

2/8
vmtrail used to be "dump last N of 16384 entries". Now `vmtrail 16 cpu3` — filtered. Forensics goes from digging through everything to ordering à la carte.

3/8
Why filter by CPU, not VM? Entries store cpu (4 bytes); VM would need topology inversion. And on our topology cpu1/2 = vm2, cpu3 = vm0.vcpu1 — CPU filter ≈ VM filter at 90%, without touching the 16K-entry structure. Honest approximation, documented.

4/8
Preempt ledger: per-VM counts at BOTH trigger points (quantum exhaustion on the pump side + frame rewrite on the exception side) + quanta. vmstat, two lines. You can't govern "who preempted whom" without numbers first.

5/8
The three inherited audit items, now with verdicts: (a) §C25 slice accounting has no cycle compensation — granularity IS the quantum (1 tick/slice), no accumulating loss path. Recorded, revisit at sub-tick quanta.

6/8
(b) §C23 CPU_ON handshake atomicity — bounded waits degrade instead of hanging on a lost poke; real-hardware event-register semantics need a fresh look on real boards. (c) §Q10 host-side eager FP save — QEMU TCG isn't a perf target, the RTOS side has been lazy since station 19C.

7/8
The discipline that matters: every "recorded, not fixed" verdict carries its reason in the ledger. A debt list without rationales is just a guilt list.

8/8
Batch B (53-56) complete: debug/PMU isolation, cross-domain ACL gate, PSCI lifecycle, DFX. Gates 161→164, smoke 10/10. Next batch: back to the RTOS — AF_UNIX sockets, virtio-net backend, ecosystem smallware. #rtos
