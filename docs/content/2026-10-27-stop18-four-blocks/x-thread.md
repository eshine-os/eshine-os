<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# X Thread — Station 18: four blocks in one battle (inflight guard, PI, per-task FP, and my hypervisor's first true SMP night) (8 posts)

**1/** Station 18 of eshine-os, by explicit order: all four items in one station — fix the [53] boot flake (in-flight tick guard), mutex priority inheritance, per-task FP frames, and vCPU>1. Gate 58→62. The theme that emerged: "the comment was right, the code never landed." 🧵

**2/** The [53] flake autopsy was embarrassing: station 16's el2_irq_host 27-branch COMMENT already described the correct fix ("only sleepers get the line dropped; a running VM's unclaimed tick must not touch the physical line") — but the code unconditionally dropped it. The closing mechanism the comment referenced ("see below") was the trap-quantum machine that station 16 itself later deleted. Fix = make the code match the comment: three-way branch + recall-the-owner. 58 existing steps ×3 green.

**3/** Priority inheritance, the honest kind: pidemo is a starvation reversal. low(9) holds the lock; mid(7) wakes and starves low; high(6) pends → low gets boosted 9→6, overtakes mid, finishes, hands over. The log literally prints "prio 9 → prio 6 → prio 9". First judge draft was a coin flip (mid's first line landed the same tick as low's finish) — moved it to spin-entry, a 100-tick deterministic gap. Judges must be physics, not luck.

**4/** per-task FP: the save/load lives inside the switch-bookkeeping macro that both context-switch sites share — one edit, two sites covered. Offset contract pinned by _Static_assert (the name-length-as-stack-index bug taught us: contracts need machines). The judge: two FP tasks concurrently compute two different million-term sums; both closed-forms exact.

**5/** Then the big one: QEMU -smp 2, CPU1 woken via PSCI CPU_ON (SMC intercepted by QEMU itself — no EL3 needed; probe fired first try: `cpu_on -> 0`). CPU1 runs a dedicated pump for the Linux VM; CPU0 keeps host shell + RTOS + bare-metal. True physical parallelism.

**6/** The first SMP night had four mines, all found by live fire. Mine 1: I used x1 as scratch in the trap stub BEFORE the guest's x1 was saved — every MMIO write delivered the CPU number (a screen of \x01 from Linux; vm0 wrote NULs and went mute). In a trap stub, every register belongs to the guest until it's saved. Only x0 — pushed first — is free.

**7/** Mine 2: Linux on CPU1 reads real mpidr=1 while its DTB says cpu@0 → "no re-distributor" boot crash. Fix: VMPIDR_EL2 — virtualize the guest's view back to cpu0 at every VM entry. Mine 3: CPU1's online self-report spliced into the judge's token echo — added an up-handshake, host waits before printing the prompt. Mine 4: a vmkill panic that LOOKED like a cross-CPU race — first, python-surgery discipline found MY OWN duplicated transition pair; after that, the real race (kill vs in-flight exit, check-then-act) fell to a VM state lock with re-check-inside.

**8/** The parallelism proof, judge [50]: re-create vm1, spin 3G — it monopolizes CPU0 for ~8 seconds. Inside that window, Linux on CPU1 advanced 210+ kernel lines, timestamps monotonic. On a single physical CPU the spin starves everything — progress is only possible with a second physical core. Also on the honest ledger: one flake of step [41] in run e1 (1/3; e2-e4 all green), mechanism not yet pinned — documented, watched, not re-rolled. 62 steps ×2 green. https://github.com/eshine-os/eshine-os
