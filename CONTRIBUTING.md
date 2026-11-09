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

## Developer Certificate of Origin

Every commit ends with exactly this trailer block, preceded by one blank
line:

    Co-Authored-By: Claude Code <noreply@anthropic.com>
    Co-Authored-By: glm-5.3 <noreply@zhipuai.cn>
    Reviewed-by: deepseek-v4.1-flash <noreply@deepseek.com>
    Signed-off-by: Julian Chu <rootuliux@gmail.com>

`Signed-off-by` certifies the [DCO](https://developercertificate.org/).
Author names and commit dates are never rewritten.

## Weekly development tags

- One lightweight tag per development week: `YY.MM-Nw`.
- `YY.MM` is the month of the week's Monday; `N` is the ordinal of the
  week among weeks containing commits in that month.
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
