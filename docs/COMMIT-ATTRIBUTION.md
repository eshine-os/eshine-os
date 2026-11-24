<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# Commit attribution

## Transition (2026-11-10)

The public history rebuilt before this date carries a uniform four-line
block on every commit:

    Co-Authored-By: Claude Code <noreply@anthropic.com>
    Co-Authored-By: glm-5.3 <noreply@zhipuai.cn>
    Reviewed-by: deepseek-v4.1-flash <noreply@deepseek.com>
    Signed-off-by: Julian Chu <rootuliux@gmail.com>

That block is a **template applied during history normalization**. It is
not proof that both models participated in every commit, nor that every
commit was reviewed. Do not cite it as per-commit evidence, and do not
copy it into new commits.

From 2026-11-10, trailers record real participation:

    Model: deepseek-v4.1-flash
    Assisted-by: glm-5.3                 # only if it actually contributed
    Reviewed-by: claude-opus-5.5         # only after a real review
    Review-Ref: docs/reviews/2026-11-10-example.md
    Signed-off-by: Julian Chu <rootuliux@gmail.com>

## Rules

- `Signed-off-by` is mandatory, exactly once, and must be the last line
  (DCO certification).
- `Model:` names the primary model for the commit. One line. Omit only for
  a genuinely human-only commit (`ALLOW_NO_MODEL=1`).
- `Assisted-by:` lists additional models that actually contributed. Omit
  when none. Never add a model for ceremony.
- `Reviewed-by:` is allowed only after a real review of that commit's
  content, and requires `Review-Ref:` pointing to an existing file under
  `docs/reviews/`. The commit-msg hook and CI reject a `Reviewed-by`
  without it.
- `Co-Authored-By:` is for human co-authors only.
- Allowed model ids: `Claude Code`, `Claude`, `claude-opus-5.5`,
  `claude-fable-5.1`, `claude-sonnet-5.5`, `claude-haiku-5.5`, `glm-5.3`,
  `deepseek-v4.1-flash`, `deepseek-v4`, `qwen3.8-max`, `gemini-4-neon`,
  `gemini-4-flash`, `antigravity`.
- Messages are English, ASCII only; subjects are `subsystem: summary`,
  at most 72 bytes.

## Enforcement

Install the repository hook once per clone:

    scripts/install-hooks.sh

Validate a range in CI or locally:

    python3 scripts/check-commit-msg.py --range <base>..<head>

Set `ALLOW_HISTORICAL_TRAILERS=1` only for history rewrites and
cherry-picks; under that flag the legacy four-line template is accepted
as-is. Set `ALLOW_NO_MODEL=1` only for a human-only commit.

## Review artifacts

A review artifact records reviewer, model, scope, findings, verdict, and
an independence statement. Use
[docs/reviews/TEMPLATE.md](reviews/TEMPLATE.md). The artifact must exist
in the same commit (or an earlier one) that carries `Review-Ref`.
Post-commit reviews are recorded as follow-up artifacts; commits are not
rewritten to add a review.

## Known exception: 2026-11-12/13 launch batch

Twelve commits dated 2026-11-12 and 2026-11-13 (launch content, the
CAL architecture diagram, Docker README, and related) carry
`Reviewed-by: deepseek-v4.1-flash` with
`Review-Ref: docs/reviews/2026-11-10-env-attribution.md`, although that
review was performed on the 2026-11-10 working tree and does not cover
those commits; some of them also carry the same model as both `Model:`
and `Reviewed-by:`. These trailers were applied as batch attestation
after the fact, which the real-semantics rules above do not permit.

This exception is recorded here for transparency. From now on every
new commit must satisfy real review semantics; no further batch
attestation is allowed.