<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# Contributing to eshine-os

## License and source headers

- Project license: Apache-2.0 (see [LICENSE](LICENSE)).
- Every source file (C, assembly, shell, Python, linker script, BitBake,
  configuration) carries `SPDX-License-Identifier: Apache-2.0` and a
  `Copyright (c) 2026 Julian Chu` line.
- JSON files cannot carry comments; they are covered by the repository
  license. Do not add SPDX keys to runtime JSON.
- Third-party components must be recorded in [3rd/LEDGER.md](3rd/LEDGER.md)
  before they enter the tree. GPL components stay outside the repository
  (sources and binaries); only build recipes are committed.

## Commit messages

- English, ASCII only; subject in the form `subsystem: summary`, at most
  72 characters.
- Body: root cause, fix, validation, residuals. Keep technical anchors
  (station/gate numbers, register and function names, SHAs).
- One logical change per commit.

## Commit attribution and DCO

Every commit ends with `Signed-off-by: Julian Chu
<rootuliux@gmail.com>`, certifying the
[DCO](https://developercertificate.org/). Record AI participation
truthfully; the historical four-line block is not a template to copy.

Trailer grammar:

    Model: <primary model>
    Assisted-by: <model>          # only if it actually contributed
    Reviewed-by: <reviewer>       # only after a real review
    Review-Ref: docs/reviews/<file>.md
    Signed-off-by: Julian Chu <rootuliux@gmail.com>

- `Model:` names the primary model for this change (one line).
- `Assisted-by:` lists additional models that actually contributed; omit
  when none.
- `Reviewed-by:` is only for a real review of this commit's content, and
  requires `Review-Ref:` pointing to an existing review artifact under
  `docs/reviews/`. The hook and CI reject it otherwise.
- `Co-Authored-By:` is for human co-authors, not for the historical AI
  template.
- Allowed model ids: `Claude Code`, `Claude`, `claude-opus-5.5`,
  `claude-fable-5.1`, `claude-sonnet-5.5`, `claude-haiku-5.5`, `glm-5.3`,
  `deepseek-v4.1-flash`, `deepseek-v4`, `qwen3.8-max`.
- Set `ALLOW_NO_MODEL=1` only for a genuinely human-only commit.
- Set `ALLOW_HISTORICAL_TRAILERS=1` only for history rewrites and
  cherry-picks; the legacy four-line template is accepted under it.

### Historical trailers are a template, not provenance

Commits before 2026-11-10 carry a uniform four-line block applied during
the public-history rebuild. It records the attribution template, not proof
that every listed model participated in every commit or that every commit
was reviewed. Do not cite it as per-commit evidence. New commits must use
the real-semantics rules above. See
[docs/COMMIT-ATTRIBUTION.md](docs/COMMIT-ATTRIBUTION.md).

## Weekly development tags

- One lightweight tag per development week: `YY.MM-Nw`.
- `YY.MM` is the month of the week's Monday; `N` is the ordinal of that
  Monday within the month (1 = first Monday of the month).
- The tag points to the earliest commit of that week on `master`
  (author date).
- Create the current week's tag with `scripts/tag-dev-week.py`; backfill
  all past weeks with `scripts/tag-dev-week.py --backfill` (idempotent).
- Release tags `vYY.MM` remain annotated; see
  [docs/VERSIONING.md](docs/VERSIONING.md).

## Clean-room and provenance

- Do not copy code from any third-party or private implementation.
- Reference public specifications only: ARM ARM, GICv3 specification,
  QEMU documentation, Linux public documentation, and 参考RTOS public
  design.
- Raise provenance questions with the maintainer before contributing.
