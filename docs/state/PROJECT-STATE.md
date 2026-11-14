<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# Project state

This file is the single-screen resume point. `scripts/update-state.sh`
refreshes the generated block; the notes below it are maintained by hand.

<!-- state:begin -->
- Updated: 2026-11-14T00:17:48Z
- HEAD: 0bbcd34 (content: re-render announcement PNGs with the dual-safety diagram)
- Version: 27.04-dev
- Gates: 183 (tools/verify/verify_el2.py)
- Dev tags: 26.08-1w .. 26.11-2w; release v26.10
<!-- state:end -->

## Current shape

- 68-station development line (`27.04-dev`): EL2 Type-1 hypervisor,
  from-scratch RTOS VM0, real Linux 6.6 guest, real musl userland, Yocto
  layer, container/release/SBOM, and interactive shell.
- 183 replayable gates. Station closure requires two consecutive full
  green runs in a quiet window; `--stop-after` runs are partial by design.
- One work root: `ESHINE_WORK` (default `<repo>/.work`). The repository
  tree is host-independent; see [DEVELOPMENT-ENV.md](../DEVELOPMENT-ENV.md).

## Open defects / risks

1. Gates `[37]`/`[38]` (dual-core demo) have a deep-contamination family;
   guards and forensics exist, root cause not fully closed.
2. `bsh` space-flood face (~1/3) has a tripwire but no self-confession yet.
3. Full 183-gate run is host-load sensitive and can exceed 30 minutes;
   futex/APS timing gates are the usual flakes.
4. QEMU virt only; real-board bring-up is not validated.
5. v0 security boundary is a roadmap target, not a completed state.

## Next action

After the 2026-11-10 attribution/isolation batch:

1. Run the full 183-gate suite twice in a quiet window and record both
   RESULT lines; do not substitute a partial run; the fast loop stops at 36, before the known [37]/[38] family.
2. Triage `[37]`/`[38]` and the `bsh` space flood with the existing
   guards; apply the defect-budget rule in
   [KNOWN-ISSUES.md](../KNOWN-ISSUES.md).
3. Resume the station sequence from the last station devlog
   (`docs/devlog/2026-11-06-station68-ghost-birth-chain.md`).

## Where things live

- Build/verify entry: `Makefile`, `tools/verify/verify_el2.py`
- Work root and bootstrap: `scripts/env.sh`, `scripts/bootstrap.sh`
- Isolation: `scripts/check-host-independence.sh`, `scripts/container.sh`
- Attribution: `docs/COMMIT-ATTRIBUTION.md`, `docs/reviews/`
- Process: `docs/STATION-PLAYBOOK.md`, `docs/MODEL-ROUTING.md`
- Gates and known issues: `docs/KNOWN-ISSUES.md`