<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# Handoff template

Copy this file to `docs/state/handoffs/YYYY-MM-DD-<topic>.md` when a
session ends mid-stream. Keep it short enough to read in one screen.

    # Handoff: <topic>

    - Date: YYYY-MM-DD
    - HEAD: <sha> (<subject>)
    - Branch/tag: <branch, nearest tag>
    - Work root: ESHINE_WORK=<path> (state: ready | partial | missing)

    ## What changed

    <files/areas; one bullet per logical change>

    ## Verification run

    | Command | Result | Evidence |
    |---|---|---|
    | `make` | pass/fail | <key output line> |
    | `verify --stop-after=N` | pass/fail | <RESULT line> |
    | full `verify` | not run | <why> |

    ## Open defects

    <symptom, first failing gate, evidence path, hypotheses ruled out>

    ## Next action

    <one concrete command or edit; no "continue the work">

    ## Do not repeat

    <approaches already refuted, with the evidence that refuted them>

    ## Session artifacts

    <logs, review files, scratch paths that matter; delete the rest>

Rules:

- A handoff without a verification table is a rumour, not a handoff.
- Link evidence by path or gate number; do not paste long logs.
- State host load and concurrent sessions if a gate flaked.