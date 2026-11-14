<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# Review: host-independent environment and real commit attribution

> Note: the commit SHAs cited in this file no longer resolve in the
> public history; rely on the descriptions and dates, not the SHAs.

- Date: 2026-11-10
- Reviewer: deepseek-v4.1-flash (read-only subagent)
- Model: deepseek-v4.1-flash
- Scope: working tree at 2026-11-10, based on HEAD 5ce7c87
- Independence: **same model family as the author**; the review ran in a
  separate read-only subagent process, but it is not a cross-family
  review. The honest label is "same model, process-independent review",
  not "independent reviewer".

## Findings

| Severity | File:line | Summary | Status |
|---|---|---|---|
| major | `scripts/check-commit-msg.py` | `ALLOW_HISTORICAL_TRAILERS=1` did not accept the legacy four-line block: Model and Review-Ref checks still fired. | fixed; legacy block now passes under the flag, targeted test added |
| minor | `scripts/check-commit-msg.py` | Body scan rejected legitimate prose trailers such as `Fixes:` and scanned the subject as body. | fixed; only attribution keys in body lines after the subject are rejected |
| minor | `scripts/check-commit-msg.py` | A body-less message with trailers on line 2 produced a misleading rejection. | fixed; subject is never parsed as a trailer, blank-line rule is reported correctly |
| minor | `Makefile`, `tools/verify/verify_el2.py`, `tools/verify/smoke.py` | `ESHINE_MUSL_DIR`/`ESHINE_CJSON_DIR`/`ESHINE_ZLIB_DIR` and `ESHINE_LINUX_DIR` overrides were ignored by Make/verify even though `scripts/env.sh` exports them. | fixed; canonical vars take precedence over legacy vars |
| minor | `scripts/check-host-independence.sh` | Static scan used `git ls-files` only, so new untracked files were skipped locally; arbitrary `/tmp/foo` was not caught. | partially fixed; untracked files and `$HOME/`/`~/` paths are scanned, guest `/tmp` paths remain intentionally exempt |
| minor | `Makefile` | Comment said demo binaries are generated, but they are tracked; filtering them from ramfs prerequisites can leave a stale committed demo in a plain `make`. | fixed comment; `make demos` remains required after demo source changes |
| minor | `.github/workflows/ci.yml` | A tag push with a zero `before` SHA could validate an old legacy commit and fail. | fixed; tag pushes skip the range check |
| minor | `docker/Dockerfile.build` | The ipxe-qemu comment overstated host behavior. | fixed wording; the container failure is recorded accurately |
| minor | repo root | No `.dockerignore`; a classic builder would send the multi-GB `.work` tree as context. | fixed; `.dockerignore` excludes `.work`, `.git`, release, and build outputs |
| minor | `ARCHITECTURE.md` | Verification prose still said "111 steps" while the suite has 183 gates. | fixed |
| minor (pre-existing) | `tools/verify/verify_el2.py` | The file has 187 `[N/183]` print sites for 183 gate numbers (`[105]` twice, `[106]` four times). This is a nominal-count discrepancy, not introduced by this batch. | accepted; not changed |
| minor | `docs/state/PROJECT-STATE.md` | The generated HEAD block is stale until `scripts/update-state.sh` is rerun after the commits. | accepted; rerun at commit time, the block cannot contain its own SHA |

## Verified positives

- Host independence: static scan plus isolated `make -n` with hostile
  `HOME`/`TMPDIR` and a fresh `ESHINE_WORK` passes. No baked
  `$HOME/projects` or fixed `/tmp/*-build` remains in the build surface.
  `scripts/bootstrap.sh` is now argument-driven and scanned.
- Clean-checkout path: a fresh copy with no `.work`, busybox, musl,
  cJSON, or zlib builds `make` successfully and reaches
  `verify --stop-after=36` PASS.
- Fast loop: gates 1..36 green, exits immediately after gate 36 with
  `PARTIAL-PASS`, 27.8s. The early exit is sound because gate 36's
  assertion precedes its print.
- Container: image built from the current Dockerfile (QEMU 8.2.2,
  gcc 13.3.0, dtc 1.7.0, gdb 15.1, python 3.12.3);
  `scripts/container.sh selftest` passed (cached build + in-container
  `make -B` + gates 1..36).
- Artifact pins: Linux 6.6 and busybox 1.36.1 sha256 values match the
  cached archives and the legacy tarballs.
- RAMFS: the current cpio has 16 entries including all demos and
  busybox; `make demos` still forces a repack; default build is fixed.
- Validator matrix: valid messages, human Co-Authored-By, and human
  Reviewed-by with an existing review artifact pass; non-ASCII, unknown
  model, unknown key, missing Model, wrong Signed-off-by, unbacked
  Reviewed-by, and the legacy block (without the historical flag) fail.

## Commands run

- `git diff --check`; `bash -n scripts/*.sh tools/*.sh`;
  `python3 -m py_compile tools/verify/*.py tools/*.py scripts/*.py`
- `scripts/check-host-independence.sh`
- validator matrix (13 cases, including historical-flag combinations)
- `make verify-fast` (PARTIAL-PASS 1..36, 27.8s)
- `scripts/container.sh selftest` (PASS, 35.8s)
- clean-checkout simulation: `make` and fast gates pass
- old-vs-new Makefile missing-`libcjson.a` test (old rc=2, new rc=0)
- sha256 pin verification; cpio entry count; docker image inspection

## Verdict

**pass-with-notes.** No blocker. The host-independent build path, fast
loop, container path, artifact pins, and trailer validator mechanics work.
The major finding (historical escape) and the actionable minors were
fixed after the review and re-checked with targeted tests; the full
183-gate suite was intentionally not run.

## Residual risks

- Full 183-gate double-green not run. Gates 37+ (including the known
  `[37]`/`[38]` SMP-demo family and the `bsh` space flood) are not
  verified by this review.
- Docker image rebuild from scratch over the network was not exercised
  (the layer cache was warm).
- Yocto gates 133/177 and `make-release.sh` end-to-end release boot were
  not run; the embedded `sock_path` ordering bug was fixed and
  syntax-checked, not booted.
- The review is same-model, not cross-family. `Reviewed-by` must not be
  read as independent third-party review.
- The working tree changed after the first snapshot; the reviewer
  re-read the final versions for the files it tested. A targeted
  re-review of the fixes is recorded in the addendum below.

## Addendum: targeted re-review

The same reviewer re-checked the eight fixes after they were applied.
Result: **all eight PASS; no new defect found.** Evidence:

1. Legacy block: default rc=1, `ALLOW_HISTORICAL_TRAILERS=1` rc=0;
   prose `Fixes:` rc=0; copied legacy block in body rc=1; subject not
   parsed as a trailer.
2. Makefile: `ESHINE_MUSL_DIR` wins over `MUSL_BUILD_DIR`; same for
   cJSON and zlib; demo comment corrected.
3. `verify_el2.py`/`smoke.py` resolve `ESHINE_LINUX_DIR` for both
   `LINUX_IMG` and `INITRAMFS`; explicit image override still wins.
4. Isolation scan now unions tracked and untracked files; the new
   scripts are in the scan set; rc=0.
5. CI skips the trailer range on tag pushes.
6. Dockerfile comment corrected: the ROM lives in
   `/usr/lib/ipxe/qemu/efi-virtio.rom`; `qemu-system-arm` only
   recommends `ipxe-qemu`, and `--no-install-recommends` would omit it.
7. `.dockerignore` covers `.git`, `.work`, release, and build outputs.
8. `ARCHITECTURE.md` says 183 gates.

Residual notes from the re-review:

- A subject followed immediately by a trailer line still fails with
  "missing blank line before trailer block". This is the deliberate
  formatting rule, not the old misleading "when a body follows" error.
- `ALLOW_HISTORICAL_TRAILERS=1` intentionally disables attribution
  policing for history replay; it must not be exported in a developer
  shell. CI never sets it.
- The full 183-gate suite was not re-run; the fast-gate and container
  results stand for the earlier snapshot, and the later verify/smoke
  changes were path-only (module import probe passed).