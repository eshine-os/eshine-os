<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# Model routing

## Tiers

| Tier | Work | Preferred model | Why |
|---|---|---|---|
| T0 | architecture, cross-subsystem forensics, security boundary | Opus / Fable class | long-horizon reasoning, holds many invariants |
| T1 | station implementation, kernel/RTOS code | GLM-5.3 or DeepSeek V4 | strong code throughput at lower cost |
| T2 | mechanical batches: translation, renames, format passes | DeepSeek V4 / Qwen | cheap, parallel, easy to verify |
| T3 | independent review | model from a different family than the author | breaks self-confirmation |

Routing is a default, not a hierarchy of worth. A T2 model can find a T0
bug; what matters is that the evidence rules below are identical.

## Context engineering

- One station per session. Start a new session at a station boundary;
  carry a handoff, not the whole transcript.
- Keep gate output and logs on disk; put the RESULT line and the first
  failing gate in context, not 2,000 lines of boot log.
- Use read-only subagents for surveys and reviews; give them paths and
  questions, not your conclusion.
- Batch mechanical edits, but keep each batch independently verifiable.

## Evidence rules

1. A claim needs a command and its output. "Should work" is not a result.
2. Before theorizing about a gate failure, check host load and concurrent
   sessions. The same suite has produced load-induced false failures.
3. Instrument before changing mechanism. A probe must first be validated
   in a known-good world; an A/B against a broken probe is no control.
4. After `git stash`, checkout, or a toolchain switch, rebuild before
   judging behaviour. A stale binary has produced a full false trail.
5. Two consecutive full green runs close a station. `--stop-after` is a
   fast loop, never a closure.

## Escalation and review

- Escalate to a stronger model after two failed fix attempts on the same
  symptom, or when the change crosses VM/EL boundaries.
- Request an independent review when a batch changes build/verify
  semantics, security boundaries, or release artifacts.
- The reviewer must state independence honestly in the artifact. Same
  model self-review is allowed but labelled; cross-family review is
  preferred.
- Every `Reviewed-by:` trailer needs a `Review-Ref:` artifact; see
  [COMMIT-ATTRIBUTION.md](COMMIT-ATTRIBUTION.md).

## Cost discipline

- Do not spend a frontier model on mechanical translation; do not spend a
  cheap model on an unresolved race.
- Close a station before opening the next; parallel stations multiply
  context and hide regressions.
- Prefer `make verify-fast` during iteration and one full double-green at
  batch close.