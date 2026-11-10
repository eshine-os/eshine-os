<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# Development metrics

## Aggregate (2026-10-20 .. 2026-11-10)

Approximate, derived from the two large local session transcripts and git
history. Event counts are tool/assistant events, not tokens.

| Metric | Value | Source |
|---|---|---|
| Commits | 214 pre-batch | `git rev-list --count HEAD` |
| Gates | 183 | `verify_el2.py` gate total |
| Stations | 68 | `docs/devlog/` sequence |
| Largest session | 107.9 MB, 46,437 lines | session transcript |
| Second session | 36.1 MB, 16,745 lines | session transcript |
| Bash calls | 8,442 | transcript tool events |
| Edit calls | 1,781 | transcript tool events |
| Read calls | 658 | transcript tool events |
| Agent calls | 50 | transcript tool events |
| Skill calls | 32 | transcript tool events |

Model event counts in the same two transcripts:

| Model | Events | Share |
|---|---|---|
| GLM-5.3 | ~22,480 | ~94% |
| Qwen 3.8 Max | 622 | ~2.6% |
| DeepSeek V4 | 566 | ~2.4% |
| Opus | 27 | ~0.1% |

Interpretation: the workflow was Bash-heavy and edit-heavy, with very
little independent review (Agent 50, Opus 27). The dominant failure mode
was not model capability but **verification discipline**: host-load
flakes, stale binaries after `git stash`, and self-inflicted probe bugs
consumed more time than code generation.

## Per-station table

Fill one row at each station close. Unknown values are `-`, not zero.

| Station | Date | Primary model | Sessions | Bash | Edits | Commits | Gates added | Defects found | Gate retries | Notes |
|---|---|---|---|---|---|---|---|---|---|---|
| 28 EL0 shell | 2026-10-31 | GLM-5.3 | - | - | - | - | 2 | 3 | - | vtimer/EL0 three-defect family |
| 42 syscall group | 2026-11-02 | GLM-5.3 | - | - | - | - | 6 | 2 | - | getpid slot vs pid |
| 63 container | 2026-11-03 | GLM-5.3 | - | - | - | - | 1 | 5 | - | environment-mask defects |
| 64 ELF core | 2026-11-03 | GLM-5.3 | - | - | - | - | 2 | 3 | - | Edit truncation, generator corruption |
| 68C-68I shell | 2026-11-04..06 | GLM-5.3 | - | - | - | 6 | 2 | 7 | - | ghost birth chain, trail-lock race |

## How to measure a session

Point the commands at one transcript file; do not commit transcripts.

    # tool distribution
    jq -r 'select(.type=="assistant") | .message.content[]? |
           select(.type=="tool_use") | .name' "$SESSION" |
      sort | uniq -c | sort -nr

    # commits and gates added by a date range
    git log --since=YYYY-MM-DD --until=YYYY-MM-DD --oneline | wc -l
    git diff --stat <base>..<head> -- tools/verify/verify_el2.py

## Targets

- Independent review on every release-cut batch, not every commit.
- Full double-green once per station batch, in a quiet window.
- Defect budget: no new feature station while a P0 is open (see
  [KNOWN-ISSUES.md](../KNOWN-ISSUES.md)).