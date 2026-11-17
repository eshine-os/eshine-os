<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# CI

## Topology

| where | config | what runs |
|---|---|---|
| GitHub Actions | `.github/workflows/ci.yml` | build + static checks + smoke gates 1..18 |
| Gitee Go | `.gitee-ci.yml` | same battery via `scripts/ci-smoke.sh` |
| local | `make verify` | full 183-gate suite (release gate) |

Cloud CI is intentionally minimal. The full suite is timing-sensitive on
shared runners and is closed on a quiet local host with two consecutive
full runs, as the station playbook requires. Cloud CI answers one
question: does the tree still build and boot the hypervisor plus the VM0
RTOS?

The shared battery (`scripts/ci-smoke.sh`, also `make ci-smoke`):

1. `make -j$(nproc)`
2. shell syntax + Python byte-compile checks
3. `verify_el2.py --stop-after=18` (banner, prompt, secondary cores,
   timer, VM create/run, FDT probes, RTOS up)

Gates above 18 are excluded on purpose: the first GitHub container run
passed 1..18 and then timed out at gate 19 (the RX wake chain) on a
loaded runner, with the guest alive and its services running. Raise the
cap with `CI_SMOKE_GATES=<n>` when the runner-load flakiness is gone.
`gen_ramfs` warns that `/bin/busybox` is missing in a clean checkout; the
smoke tier never reaches the busybox gates, so the warning is harmless.

## GitHub Actions

Runs on pushes to `master` and weekly `dev/**` branches, on pull
requests, and on manual dispatch. Both jobs run on `ubuntu-24.04`, not
`ubuntu-latest`.

The failure that motivated the pin: `ubuntu-latest` moved to 26.04,
whose `qemu-system-arm` is 10.2.1. Under QEMU 10.2.1 the RTOS takes an
EL2 alignment fault during boot (`ESR_EL2=0x96000061`, DFSC=alignment
fault) and halts in the exception handler before printing `eshine>`.
Runs `43535269610`, `43562198218`, `43811739099`, `43853221275`,
`43853673774` and `43854057149` all failed this way or at gate 19.
24.04 ships QEMU 8.2.2, the version the gates were developed against
(see `3rd/manifest.yaml`).

The `container` job repeats the battery inside
`docker/Dockerfile.build` (Ubuntu 24.04, `--network none`), so the CI
result does not depend on the runner image's toolchain.

## Gitee Go

The Gitee repository is a mirror of GitHub; `.gitee-ci.yml` arrives with
the mirror sync. Gitee Go is UI-configured and disabled by default, so
one manual step is required:

1. Gitee repository page -> 服务 (Services) -> Gitee Go -> enable.
2. The pipeline `ci` appears from `.gitee-ci.yml`; triggers are push to
   `master` and PRs to `master`.
3. If the pipeline editor asks for approval, confirm the YAML source is
   the repository file rather than the UI draft.

The pipeline uses the `build@gcc` step (Ubuntu-based shell environment)
and installs the cross toolchain plus QEMU from apt before calling
`scripts/ci-smoke.sh`. If the Gitee image cannot reach apt or ships an
incompatible QEMU, switch the step to 基于镜像的脚本执行 (image-based
script execution) with image `ubuntu:24.04` and the same commands; this
keeps the QEMU 8.2.2 pin that GitHub Actions uses.

## Local

    make ci-smoke     # build + static checks + gates 1..18
    make verify       # full suite (release gate)
    make verify-fast  # gates 1..36, iteration loop

`CI_SMOKE_GATES` and `CI_NO_BUILD=1` control `scripts/ci-smoke.sh`
directly when a different environment needs a different cap.