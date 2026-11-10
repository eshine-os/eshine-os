<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# Reviews

Review artifacts back the `Reviewed-by:` and `Review-Ref:` commit
trailers. A trailer without a real review is a false claim; do not add
one.

## Naming

`YYYY-MM-DD-<slug>.md`, for example `2026-11-10-isolation.md`.

## Required content

Use [TEMPLATE.md](TEMPLATE.md). At minimum:

- reviewer identity and model (or "human")
- review date
- scope: base commit, head commit, or diff file
- independence statement: different model family from the author, or
  "same model, self-review" (state it honestly)
- findings (severity, file, summary)
- verdict: pass / pass-with-notes / fail
- residual risks and tests run

## Referencing

A commit with `Reviewed-by: <reviewer>` must include:

    Review-Ref: docs/reviews/<file>.md

The file must exist in that commit or an earlier commit. Post-commit
reviews are recorded as follow-up artifacts rather than rewriting the
reviewed commit.