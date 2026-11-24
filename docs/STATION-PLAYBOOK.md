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
   RESULT line (green suite) is recorded.
4. Three-way sync in the same commit: the gate registry under
   `tools/verify/gatesuite/gates/`, the shell help walkthrough, and the
   verification section of `ARCHITECTURE.md`.
5. Devlog in `docs/devlog/`; external media/social drafts are maintained
   outside the core OS repository.
6. Commit message has real trailers and passes
   `scripts/check-commit-msg.py`.
7. Mirror synced through the maintainer's channel.
8. Memory updated with the non-obvious lesson, not the changelog.

## Loop

1. **Spec** - problem, mechanism, gate criteria, out-of-scope.
2. **Plan** - files, invariants, failure modes, verification commands.
3. **Implement** - smallest correct change; keep the old path working
   until the new gate proves it.
4. **Smoke** - run the new scene manually; capture the exact output.
5. **Gate** - add the gate to the registry and run it in isolation
   (`gate_suite.py --gates <id>`); at station close run `make verify`
   (green suite) twice. `make verify-fast` is the smoke iteration loop.
6. **Forensics** - on failure, preserve evidence before editing:
   gate number, first divergent line, host load, QEMU/GDB state.
7. **Sync** - three-way update; walk the shell help with the real shell,
   do not sed-splice it.
8. **Close** - devlog, content package, commit, mirror, memory.

## Batch mode

- Multiple stations may be queued when the user authorizes continuous
  progress, but each station still closes its own gates.
- `make verify-fast` (smoke, gates 1..18) is the iteration loop; a batch
  closes with two consecutive green-suite runs, not a collection of
  partial runs.
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

- IDs are stable documentation anchors. The historical namespace is
  1..183; new gates start at 184. Retired gates leave gaps and are never
  reused (`--list --include-retired`).
- An ordinary gate runs in its own fresh fixture. If the gate is
  inherently stateful, add it to a scenario fixture instead and keep the
  scenario self-contained (actions always run; assertions are gated).
- A scenario failure marks its remaining gates SKIP but must not block
  other fixtures. Run the green suite after insertion, not just the new
  gate.
- Gate lines still render as `[N/183] description`; the parser and
  `scripts/update-state.sh` depend on that shape.
- Red gates are classified (`REAL-DEFECT`, `LOAD-FLAKE`, `MIGRATION-BUG`,
  `ENV`, `OBSOLETE`) and recorded in `tools/verify/quarantine.txt` until
  evaluated; heavy Yocto/image/Docker gates live in the `heavy` suite.

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
- The public remote is updated only through the maintainer's channel;
  do not push from development hosts.