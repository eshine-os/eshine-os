<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# CI and the gate suite

## Topology

| where | config | what runs |
|---|---|---|
| GitHub Actions | `.github/workflows/ci.yml` | smoke (gates 1..18) + artifacts (guest artifact cache) + green (3 shards, run inside the pinned toolchain image) |
| Gitee Go | `.gitee-ci.yml` | the same green suite via `scripts/ci-gates.sh` |
| local | `make verify` | green suite; two consecutive runs close a station |
| local fast | `make verify-fast` / `make ci-smoke` | smoke (gates 1..18) |
| local heavy | `make verify-heavy` | Yocto/image gates (133/177/178) + Docker static (180) |

The suite is fixture-based. Every ordinary gate runs against a fresh
QEMU session, so a red gate cannot block the others and failures are
reported individually. A few families are inherently stateful (RTOS boot
stream, mutex mid-hold, VM1 lifecycle, Linux boot, multi-VM hotkeys, ACL,
logging, HAM) and run as one scenario fixture with several ordered gate
results. A failed scenario check is retried on a fresh fixture like an
ordinary gate; if it still fails, that gate is FAIL and the rest of that
scenario SKIP, while other fixtures keep running.

To keep the wall clock down, vm0 command gates whose face returns to the
prompt without changing global VM state share a boot in batches of up to
five (`registry.BATCHES`): each gate still runs and reports its own
PASS/FLAKY/FAIL, a failing gate is retried on a fresh boot, and a fixture
that cannot be established is retried up to `--boot-retries`. Gates that
pause or kill the VM, hand the console back to the host, or crash it
(25, 26, 122-126, 130, 131) keep a private boot. This cut the green suite
from ~800 s to ~370-400 s locally and to 501 s on the GitHub runner
(2026-11-18; 173/173 green both ways).

Gate IDs are stable documentation anchors. The historical namespace is
1..183; retired gates leave gaps (`--list --include-retired`) and new
gates start at 184. The runner keeps the `[N/183]` line format for
existing tooling.

## Commands

    python3 tools/verify/gate_suite.py --suite smoke
    python3 tools/verify/gate_suite.py --suite green --retries 3
    python3 tools/verify/gate_suite.py --suite green --shard 2/3
    python3 tools/verify/gate_suite.py --gates 1-18,20,27-29
    python3 tools/verify/gate_suite.py --fixture vm0
    python3 tools/verify/gate_suite.py --list
    python3 tools/verify/gate_suite.py --count
    python3 tools/verify/gate_suite.py --validate

Every gate has a wall-clock cap (`--gate-cap`, default 300s; scenarios
`--scenario-cap`, default 1800s) so a hung SUT fails the gate instead of
stalling the run. The ported per-gate reads use observed-scale budgets,
not the old serial-suite timings.

Statuses: `PASS`, `FLAKY` (passed on a retry, visible), `FAIL`, `SKIP`
(not selected / blocked downstream of a scenario failure / missing
artifact in non-strict mode), `BLOCKED` (missing artifact in strict mode
or a fixture that could not be established). The final line is
`RESULT PASS|FAIL|PARTIAL-PASS ... red=[ids]`.

Fixture keys: `host`, `vm0` (VM0 attached), `vm0ready` (VM0 booted,
console host), `vm0boot`, `vm1`, `dual` (VM0 running + VM1 booted),
`vm2` (Linux attached), `vm2boot` (Linux booting), `vm2boot0`, `multi`
(all three VMs).

Known boot families are handled at the fixture level: zero-byte chardev
boots restart the QEMU (6 attempts in CI via `CI_BOOT_RETRIES`, 45s
cooldown; the CLI default is 3); the all-space flood
(`wvtrip: all-space chunk`) raises `FloodError` and a fresh fixture is
tried (the flood retry is immediate, so the extra attempts are cheap); a fixture that cannot reach its readiness anchors is retried the
same way. These retries are visible as `FLAKY` when a gate eventually
passes.

## Shards and run time

The green suite is split into three shards (`--shard K/3`).  Sharding
is applied to whole scenario/batch/gate groups, so a stateful scenario
or a shared-boot batch is never split across runners; each shard runs
on its own runner in parallel.

Guest artifacts are built once per cache generation by the `artifacts`
job.  Its cache key is pinned to `tools/artifacts.json` and
`3rd/manifest.yaml` (the artifact versions), not to build-script mtimes,
so editing a build script no longer triggers a cold Linux/musl/busybox
rebuild (~17 min in runs 44093367769/44093324790).  The green shards run
inside the pinned `docker/Dockerfile.build` image, which removes the
per-run `apt-get install` (it cost 17-24 min on two hosted runners in
runs 44225068223/44226378906).  Every shard still runs `prepare` inside
the image (a ~20 s `make` plus the inline static checks and the ramfs
busybox assertion), because `eshine.elf`/`rtos.bin` are build products
that are deliberately not cached.

## Cloud artifacts

The green suite needs the GPL-isolated artifacts that are never
committed: the Linux Image, the exactly-8MiB initramfs and the musl
busybox. `scripts/ci-gates.sh prepare` runs `scripts/bootstrap.sh --fetch`
(builds Linux/musl/cJSON/zlib/busybox/initramfs) **before** the base
`make`: the kernel embeds `rtos/ramfs.cpio` at link time, so packing the
ramfs before busybox exists produces an image where every `run /bin/...`
gate fails with `run: open failed`. A hard assertion after `make` now
refuses to run the suite when `cpio -it rtos/ramfs.cpio` has no
`bin/busybox`. GitHub Actions caches `.work` and
`tools/ramfs/bin/busybox` keyed by `tools/artifacts.json` plus the build
scripts, so subsequent runs skip the Linux build. `CI_SKIP_FETCH=1` is
set on a cache hit; the busybox is restored from the cache in that case.
The run step uses `--strict-artifacts`: a missing required artifact is
BLOCKED (CI fails) rather than silently skipped.

The container job runs only the smoke tier: the pinned container has no
network and therefore cannot fetch the guest artifacts. It still proves
that the pinned Ubuntu 24.04 toolchain boots the hypervisor and VM0.

## Red-gate policy

Red gates are not hidden. They are either:
- `quarantined` in `tools/verify/quarantine.txt` (`id classification
  reason review-by`), excluded from the green suite but still run by
  `--suite all`; or
- `heavy` (Yocto/image/Docker), excluded from cloud CI by design and run
  by `make verify-heavy`; or
- `retired` when their surface is folded into a canonical gate (IDs are
  never reused).

The classification labels are `REAL-DEFECT`, `LOAD-FLAKE`, `MIGRATION-BUG`,
`ENV` and `OBSOLETE`. A red gate is evaluated one by one; the evidence is
the failing gate line plus the per-gate log under `.work/logs/`. The
first triage pass (five entries, with evidence and options) is recorded
in [GATE-TRIAGE.md](GATE-TRIAGE.md).

## Runner pin and QEMU

Both GitHub jobs run on `ubuntu-24.04`, not `ubuntu-latest`. The
`ubuntu-latest` image moved to 26.04, whose `qemu-system-arm` is 10.2.1.
Under QEMU 10.2.1 the RTOS takes an EL2 alignment fault during boot
(`ESR_EL2=0x96000061`, DFSC=alignment fault) and halts before printing
`eshine>`. Runs `43535269610`, `43562198218`, `43811739099`,
`43853221275`, `43853673774` and `43854057149` all failed this way or at
the load-sensitive gate 19. 24.04 ships QEMU 8.2.2, the version the gates
were developed against (see `3rd/manifest.yaml`).

## Gitee Go

The Gitee repository mirrors GitHub; `.gitee-ci.yml` arrives with the
mirror sync. Gitee Go is UI-configured and disabled by default, so one
manual step is required:

1. Gitee repository page -> 服务 (Services) -> Gitee Go -> enable.
2. The pipeline `ci` appears from `.gitee-ci.yml`; triggers are push to
   `master` and PRs to `master`.
3. If the pipeline editor asks for approval, confirm the YAML source is
   the repository file rather than the UI draft.

The pipeline uses the `build@gcc` step (Ubuntu-based shell environment)
and runs `scripts/ci-gates.sh all` with `CI_SUITE=green`. If the Gitee
image cannot reach apt or ships an incompatible QEMU, switch the step to
基于镜像的脚本执行 (image-based script execution) with image
`ubuntu:24.04` and the same commands; this keeps the QEMU 8.2.2 pin.