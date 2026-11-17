<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# Project state

This file is the single-screen resume point. `scripts/update-state.sh`
refreshes the generated block; the notes below it are maintained by hand.

<!-- state:begin -->
- Updated: 2026-11-17T08:03:03Z
- HEAD: 70dc53d (docs: sync verification docs with the fixture gate suite)
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
  scenario fixture). The green suite excludes quarantined reds
  (`tools/verify/quarantine.txt`). Station closure requires two
  consecutive green runs in a quiet window; `--stop-after` runs are
  partial by design.
- One work root: `ESHINE_WORK` (default `<repo>/.work`). The repository
  tree is host-independent; see [DEVELOPMENT-ENV.md](../DEVELOPMENT-ENV.md).

## Open defects / risks

1. Gates `[37]`/`[38]` (dual-core demo) have a deep-contamination family;
   guards and forensics exist, root cause not fully closed.
2. `bsh` space-flood face (~1/3) has a tripwire but no self-confession yet.
3. The full green suite is host-load sensitive: 1..77 took 18 minutes
   with nine FLAKY gates on a load-12 host (2026-11-17). Fixture-level
   retries absorb the known boot/attach families; futex/APS timing gates
   are the usual timing flakes. Gate `[122]` (APS hang) is quarantined.
4. QEMU virt only; real-board bring-up is not validated.
5. v0 security boundary is a roadmap target, not a completed state.

## Next action

Week 26.11-3w (from 2026-11-16). Ordered by the defect-budget rule:
known P0-class defects close before new feature stations.

1. Close the `[37]`/`[38]` dual-core contamination family. Root-a is
   fixed; root-b/root-c forensics are armed (leads: `sp=0xC`,
   self-switch `next==old`, CPU1 six-beat window in the `ipimain`
   stack region). Gate `[37]` is currently a deterministic signature
   failure (2/2), which blocks any full double-green run.
2. After the family closes: full green suite, two consecutive green
   runs in a quiet window, both RESULT lines recorded. The fast loop
   stops at 36 by design and is not a release criterion.
3. `bsh` space-flood face (~1/3 of runs): triage with the existing
   tripwire; a self-confessed root cause closes KNOWN-ISSUES #2.
4. Station 69 candidates, picked once the defect budget clears
   (see the roadmap priorities on the project homepage):
   - security line: harness-driven fuzzing of the EL2
     syscall/virtualization boundary;
   - determinism line: sub-tick hrtimer wake-ups;
   - real-hardware line: SMMU / GIC-ITS bring-up study.
5. Release face: keep the weekly dev-tag ritual
   (`scripts/tag-dev-week.py`, Monday), the weekly dynamic content
   package, and mirror sync health (GitHub/Gitee pushes remain
   manual).

## Where things live

- Build/verify entry: `Makefile`, `tools/verify/gate_suite.py`
- Work root and bootstrap: `scripts/env.sh`, `scripts/bootstrap.sh`
- Isolation: `scripts/check-host-independence.sh`, `scripts/container.sh`
- Attribution: `docs/COMMIT-ATTRIBUTION.md`, `docs/reviews/`
- Process: `docs/STATION-PLAYBOOK.md`, `docs/MODEL-ROUTING.md`
- Gates and known issues: `docs/KNOWN-ISSUES.md`