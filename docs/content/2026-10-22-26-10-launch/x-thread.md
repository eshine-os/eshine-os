<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
<!-- X thread (English), 8 tweets; 也可截中文版另发 -->

# X / Twitter thread

**1/8**
Day 1 of building an open-source Type-1 hypervisor from scratch — 1-2 hours a day, pair-programming with AI (Claude + GLM-5.3).

Tonight: from zero lines to a tagged, released v26.10 in one evening. 🧵

📌 [attach: banner + shell screenshot]

**2/8**
eshine-os = a foundational OS for safety-critical domains (autonomous driving, robotics, medical). Target: Type-1 hypervisor at EL2, RTOS management VM + Linux guests on top. Mission: "the light beneath, guarding life."

https://www.eshine-os.org

**3/8**
Why from scratch? Two reasons:
1/ Every line written from public specs (ARM manuals, QEMU docs). One clean Apache-2.0 license, SPDX on every file. No mystery bytes.
2/ Design philosophy: VM lifecycle BEFORE features. The very first commit already has a table-driven state machine — any transition outside the table panics. Default-deny.

**4/8**
Boot: QEMU `-M virt,virtualization=on` hands us EL2 directly (no ATF/EL3 in the emulated phase — scope management, not laziness; the real-board EL3 path is a documented roadmap note).

Entry: check CurrentEL == EL2 (park in WFI otherwise), mask DAIF, clear BSS, install VBAR_EL2, set stack, jump to C.

**5/8**
The 16-slot vector table: only current-EL sync (dumps ESR_EL2/FAR_EL2) and IRQ (WFI) are implemented. The other 12 slots all land in ONE default-deny handler.

Crash loudly, never swallow silently.

**6/8**
The part I care most about — the true-green gate.

Every commit, a script boots the system on QEMU, waits for the banner, gets a shell prompt, then sends `echo ESHINEOK<pid>` and demands the token back TWICE (typed-echo + command output). Static markers don't count. Static markers lie.

**7/8**
Tonight that gate caught 3 real bugs — including its own (it matched the first prompt in a stale buffer and "passed" without waiting) and a stale banner baked into an object file because header deps weren't tracked (now: kbuild-style -MMD/-MP).

AI-written code, caught by an AI-era gate. That's the loop.

**8/8**
v26.10 is tagged: 528 lines of C/asm, 18 files, kernel-style commits with AI co-authorship right there in git log, Ubuntu-style YY.MM versioning (VERSION file = single source of truth, injected into the build AND the gate).

Next: 27.04 — stage-2 page tables, first guest address space.

⭐ https://github.com/eshine-os/eshine-os

---

<!-- 附图清单:1) banner+shell  2) RESULT PASS  3) repo README  4) state demo  5) 官网 -->
<!-- 首推可单独发 banner 截图 + "Day 1: green on night one." -->
