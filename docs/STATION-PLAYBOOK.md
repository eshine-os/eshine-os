<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# Station playbook

A station is one vertical slice with replayable gates. Stations are the
unit of planning, closure, and memory.

## Definition of done

A station is closed only when all of the following are true:

1. Spec exists in `docs/specs/` (or the station is explicitly a
   survey/forensics station with a written scope).
2. Implementation builds clean: `make` with no new warnings.
3. The new gate(s) pass twice consecutively in a quiet window, and the
   RESULT line is recorded.
4. Three-way sync in the same commit: `tools/verify/verify_el2.py`, the
   shell help walkthrough, and the verification section of
   `ARCHITECTURE.md`.
5. Devlog in `docs/devlog/` and the five-file content package under
   `docs/content/` (README, Zhihu/WeChat, Xiaohongshu, X thread, YouTube).
6. Commit message has real trailers and passes
   `scripts/check-commit-msg.py`.
7. VPS mirror synced (see the local push pipeline; never push to GitHub
   from this host).
8. Memory updated with the non-obvious lesson, not the changelog.

## Loop

1. **Spec** - problem, mechanism, gate criteria, out-of-scope.
2. **Plan** - files, invariants, failure modes, verification commands.
3. **Implement** - smallest correct change; keep the old path working
   until the new gate proves it.
4. **Smoke** - run the new scene manually; capture the exact output.
5. **Gate** - add the gate, run `verify --stop-after=<gate>` first, then
   the full suite twice at station close.
6. **Forensics** - on failure, preserve evidence before editing:
   gate number, first divergent line, host load, QEMU/GDB state.
7. **Sync** - three-way update; walk the shell help with the real shell,
   do not sed-splice it.
8. **Close** - devlog, content package, commit, VPS, memory.

## Batch mode

- Multiple stations may be queued when the user authorizes continuous
  progress, but each station still closes its own gates.
- `verify-fast` (`--stop-after=36`, before the open [37]/[38] family)
  is the iteration loop; a batch closes with a full double-green, not a
  collection of partial runs.
- If the full suite is blocked by host load, record the exact failing
  gates and rerun in a quiet window; do not declare green.

## Failure triage order

1. Host load / concurrent sessions / stale binary.
2. Probe and parser correctness (the instrument may be the bug).
3. World-state assumptions (a new gate can invalidate a downstream
   escape step).
4. Mechanism under test.
5. Specification ambiguity.

Record refuted hypotheses. A later session must not re-walk them.

## Gate insertion rules

- Gates are append-only after a station closes; do not renumber.
- A new gate that changes world state can invalidate later gates. Run the
  full suite after insertion, not just the new gate.
- Every gate line is `[N/183] description`; the parser depends on it.
  Do not add whitespace or alternate formatting.

## Environment hygiene

- All artifacts under `ESHINE_WORK`; no `$HOME` or fixed `/tmp` paths.
- Use `scripts/bootstrap.sh --adopt` for offline migration and
  `scripts/container.sh` for a pinned toolchain.
- Run `scripts/check-host-independence.sh` before a build-system change.

## Commit and memory

- One logical change per commit; real trailers only; `Signed-off-by`
  last.
- The devlog records what happened; memory records what will change
  future behaviour (a rule, a trap, a decision).
- Do not push to GitHub from this host; the VPS mirror is the sync path.