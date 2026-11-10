<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# Development environment

## Single work root

All external artifacts, build state, sockets, and logs live under one
directory:

    ESHINE_WORK (default: <repo>/.work)

    .work/
      linux/          Linux Image, initramfs.cpio, linux-6.6 source
      musl-build/     musl source + inst/
      cjson-build/    cJSON source + libcjson.a
      zlib-build/     zlib source + libz.a
      busybox-build/  busybox source + working tree
      poky/           Yocto poky clone (optional)
      yocto-build/    Yocto build tree (optional)
      cache/downloads/ pinned tarballs
      run/            per-run sockets and logs
      logs/           build logs
      tmp/            scratch

`scripts/env.sh` is the single source of these paths. Every build script
sources it; the Makefile and `verify_el2.py`/`smoke.py` use the same
defaults. No script reads `$HOME/projects` or a fixed `/tmp` build dir.

## Bootstrap

    scripts/bootstrap.sh --adopt --from <legacy-root> \
        --musl <musl-dir> --cjson <cjson-dir> --zlib <zlib-dir>
    scripts/bootstrap.sh --fetch   # download pinned sources and build all
    scripts/bootstrap.sh --all     # adopt if legacy inputs are given, else fetch
    scripts/bootstrap.sh --check   # verify the work root

Pinned external artifacts (`tools/artifacts.json`):

| artifact | version | sha256 |
|---|---|---|
| linux | 6.6 | `d926a06c...e56d0` |
| busybox | 1.36.1 | `b8cc24c9...e314` |

In-repo `3rd/{musl,cjson,zlib}` tarballs are pinned in `3rd/manifest.yaml`
and are offline by design.

`--adopt` copies from directories you name on the command line (or via
the `ESHINE_LEGACY_ROOT`, `ESHINE_LEGACY_MUSL_DIR`,
`ESHINE_LEGACY_CJSON_DIR`, `ESHINE_LEGACY_ZLIB_DIR` environment
variables). No host path is baked into the repository; normal builds
never read these directories. `--with-yocto` uses `--from` for `poky/`
and `eshine-yocto-build/`. `tools/ramfs/bin/busybox` remains the one
gitignored output in the repo because the vm0/vm2 "same bytes" invariant
depends on that path.

## Host build

Requirements: `aarch64-linux-gnu-gcc`, `qemu-system-aarch64`, `python3`,
`make`; `dtc` is optional (the committed DTB is used with a warning).
`gdb-multiarch` is optional (failure forensics only).

    make
    make bootstrap
    make verify-fast
    make verify

## Container build

    scripts/container.sh build
    scripts/container.sh make -j"$(nproc)"
    scripts/container.sh verify --stop-after=36
    scripts/container.sh selftest

The image (`docker/Dockerfile.build`) pins the cross toolchain, QEMU,
dtc, python3, gdb-multiarch, and Linux build dependencies. The repo is
mounted at `/src`; `ESHINE_WORK` is mounted at `/work`. `verify` uses
`--network none`; `build` needs network for apt.

If Docker is unavailable or apt/image pulls are blocked, use the host
build path (`bootstrap.sh --fetch`, or `--adopt` with explicit legacy
directories); the repository files are identical.

## Full verification

`make verify` runs all 183 gates and requires:

- `.work/linux/.../Image` and `.work/linux/initramfs.cpio` (exactly 8 MiB)
- `tools/ramfs/bin/busybox` (gitignored; build with `tools/build-busybox.sh`)
- `poky` + `yocto-build` under `.work` (gates 133/177)
- a quiet host: full two-run green is host-load sensitive

`make verify-fast` runs gates 1-36 and needs no external artifacts; it stops before the open [37]/[38] SMP-demo family, which the full suite still covers. The
preflight only requires Linux/initramfs at gate 62 and busybox at gate
103; a full run still fails loudly when they are missing.

## Isolation check

    make check-isolation

Static-scans build/code files for host paths (including
`scripts/bootstrap.sh`; legacy locations are arguments, not defaults) and
runs `make -n` with `HOME`/`TMPDIR` pointing into a throwaway directory
and a fresh `ESHINE_WORK`. Historical devlogs/specs are records, not build
dependencies, and are not part of the strict scan.