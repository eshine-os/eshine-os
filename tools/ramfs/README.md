<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# tools/ramfs

Root filesystem tree packed into `rtos/ramfs.cpio` at build time by
`tools/gen_ramfs.py` (cpio newc).

## Prebuilt demo binaries (`bin/`)

Small first-party demo binaries are checked in so a fresh clone can
build and boot without a musl toolchain. They are Apache-2.0, built from
`tools/ramfs-*.c`:

| binary | source | rebuild |
|---|---|---|
| `beat` | `tools/ramfs-beat.c` | `tools/build-musl.sh` |
| `coreparse` | `tools/ramfs-coreparse.c` | `make demos` |
| `cyclictest` | `tools/ramfs-cyclictest.c` | `make demos` |
| `hello` | `tools/ramfs-hello.c` | `tools/build-musl.sh` |
| `initmark` | `tools/ramfs-initmark.c` | `tools/build-musl.sh` |
| `jsontest` | `tools/ramfs-jsontest.c` | `make demos` |
| `pt` | `tools/ramfs-pt.c` | `tools/build-musl.sh` |
| `sockdemo` | `tools/ramfs-sockdemo.c` | `tools/build-musl.sh` |
| `ztest` | `tools/ramfs-ztest.c` | `make demos` |

`tools/build-musl.sh` builds the musl libc first; `make demos` expects
the cJSON and zlib builds under `/tmp` (see `tools/build-cjson.sh` and
`tools/build-zlib.sh`).

## GPL isolation

`bin/busybox` is **not tracked**. Busybox is GPL-2.0 and stays outside
the repository; `tools/build-busybox.sh` produces it under
`tools/ramfs/bin/busybox` (gitignored) for local packaging only.
