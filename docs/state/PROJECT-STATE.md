<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# Project state

This file is the single-screen resume point. `scripts/update-state.sh`
refreshes the generated block; the notes below it are maintained by hand.

<!-- state:begin -->
- Updated: 2026-11-24T02:40:55Z
- HEAD: de35120 (docs: update architecture diagram and add terminal demo gif)
- Version: 27.04-dev
- Gates: 177 active in the 1..183 namespace (tools/verify/gate_suite.py)
- Dev tags: 26.08-1w .. 26.11-3w; release v26.10
<!-- state:end -->

## Current shape

- 68-station development line (`27.04-dev`): EL2 Type-1 hypervisor,
  from-scratch RTOS VM0, real Linux 6.6 guest, real musl userland, Yocto
  layer, container/release/SBOM, and interactive shell.
- 177 active gates in the stable 1..183 namespace, run as independent
  fixtures (a fresh QEMU per ordinary gate; stateful families use one
  scenario fixture; up to five read-only vm0 gates batch per boot).
  On 2026-11-18, GitHub Actions (`ubuntu-24.04`, QEMU 8.2.2) passed
  **173/173 green gates in 403 s wall** across 3 shards (run 44265186558).
  4 gates remain quarantined in `tools/verify/quarantine.txt`
  (`122`, `135`, `140`, `182`) after `f8a9d5b` un-quarantined
  `103/141/172/173`.
- One work root: `ESHINE_WORK` (default `<repo>/.work`). The repository
  tree is host-independent; see [DEVELOPMENT-ENV.md](../DEVELOPMENT-ENV.md).

## Open defects / risks

1. Quarantined gates (`122` APS shell starvation; `135/140/182` obsolete
   test anchors; `review-by: 2026-11-24`) block a 177/177 zero-quarantine
   run.
2. Gates `[37]`/`[38]` (dual-core demo) pass under fixture retries, but
   their deep-contamination family awaits quiet-window double-green
   closure; `bsh` space-flood (`~1/3` runs) awaits root-cause triage.
3. EL2 Stage-1 MMU is not yet enabled (QEMU 10.2.1 hits `ESR_EL2=0x96000061`
   alignment fault), pinning CI to Ubuntu 24.04 / QEMU 8.2.2.
4. Security-boundary & ARM64-primitive audit findings pending fix:
   RTOS `access_ok` / `sys_read` user-pointer bounds and copy fixup table,
   W^X page flags, SMP `tlbi vmalle1is`, 64-bit `ldaxr/stxr` widths,
   Stage-2 `tlbi ipas2e1is` / `SH[9:8]` encoding.
5. QEMU `virt` only; real-board bring-up is not yet validated and formal
   third-party certification has not started.

## Next action

Ordered by the defect-budget rule (safety/architecture invariants before
new feature surface):

1. Close the 4 quarantined gates (`122/135/140/182`) and confirm 177/177
   green in a quiet-window double-green run.
2. Fix P0 ARM64 primitives & RTOS/EL2 security boundaries (spinlock width
   and `console_unlock` race, strict `access_ok` + `.ex_table` uaccess,
   Stage-2 `SH[9:8]` + TLBI, RTOS W^X + `vmalle1is`), backed by new
   negative security gates (`184+`).
3. Enable EL2 Stage-1 MMU and verify compatibility on QEMU 10.2+ alongside
   QEMU 8.2.2; close `bsh` space-flood and `[37]/[38]` stability items.
4. Modularize oversized C/header files (`rt_shell.c`, `rt_syscall.c`,
   `vm.c`, `rtos.h`) and prepare Board HAL for real ARM64 hardware
   bring-up (e.g., Raspberry Pi 5 / RK3588 -> automotive SoC).

## Where things live

- Build/verify entry: `Makefile`, `tools/verify/gate_suite.py`
- Work root and bootstrap: `scripts/env.sh`, `scripts/bootstrap.sh`
- Isolation: `scripts/check-host-independence.sh`, `scripts/container.sh`
- Attribution: `docs/COMMIT-ATTRIBUTION.md`, `docs/reviews/`
- Process: `docs/STATION-PLAYBOOK.md`, `docs/MODEL-ROUTING.md`
- Gates and known issues: `docs/KNOWN-ISSUES.md`
