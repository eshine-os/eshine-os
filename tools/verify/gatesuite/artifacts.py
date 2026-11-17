# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Julian Chu
"""Environment paths and artifact preflight (mirrors scripts/env.sh)."""

from __future__ import annotations

import dataclasses
import os

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__)))))
WORK = os.environ.get("ESHINE_WORK", os.path.join(REPO, ".work"))
RUN_DIR = os.environ.get("ESHINE_RUN_DIR", os.path.join(WORK, "run"))
LOG_DIR = os.environ.get("ESHINE_LOG_DIR", os.path.join(WORK, "logs"))
QEMU = os.environ.get("ESHINE_QEMU", "qemu-system-aarch64")
GDB = os.environ.get("ESHINE_GDB", "gdb-multiarch")
ELF = os.path.join(REPO, "eshine.elf")
RTOS_BIN = os.path.join(REPO, "rtos.bin")
GUEST2 = os.path.join(REPO, "guest2.bin")
LINUX_DIR = os.environ.get("ESHINE_LINUX_DIR", os.path.join(WORK, "linux"))
LINUX_IMG = os.environ.get(
    "ESHINE_LINUX_IMAGE",
    os.path.join(LINUX_DIR, "linux-6.6", "arch/arm64/boot/Image"))
INITRAMFS = os.environ.get(
    "ESHINE_INITRAMFS", os.path.join(LINUX_DIR, "initramfs.cpio"))
BB = os.environ.get("ESHINE_BUSYBOX",
                    os.path.join(REPO, "tools/ramfs/bin/busybox"))
VERSION = open(os.path.join(REPO, "VERSION"), encoding="utf-8").read().strip()
BANNER = ("eshine-os " + VERSION).encode()


@dataclasses.dataclass(frozen=True)
class Env:
    repo: str = REPO
    work: str = WORK
    run_dir: str = RUN_DIR
    log_dir: str = LOG_DIR
    qemu: str = QEMU
    gdb: str = GDB
    elf: str = ELF
    rtos_bin: str = RTOS_BIN
    guest2: str = GUEST2
    linux_img: str = LINUX_IMG
    initramfs: str = INITRAMFS
    busybox: str = BB
    version: str = VERSION
    banner: bytes = BANNER

    def ensure_dirs(self) -> None:
        os.makedirs(self.run_dir, exist_ok=True)
        os.makedirs(self.log_dir, exist_ok=True)


def load_env() -> Env:
    env = Env()
    env.ensure_dirs()
    return env


def artifact_problems(tag: str, env: Env) -> list[str]:
    """Return human-readable reasons why artifact group `tag` is unusable."""
    out: list[str] = []
    if tag == "none":
        return out
    if tag == "elf":
        for p in (env.elf, env.rtos_bin, env.guest2):
            if not os.path.exists(p):
                out.append("missing %s (run make)" % p)
    if tag == "linux":
        if not os.path.exists(env.linux_img):
            out.append("missing Linux Image %s (scripts/build-linux.sh)"
                       % env.linux_img)
        if not os.path.exists(env.initramfs):
            out.append("missing initramfs %s (scripts/build-initramfs.sh)"
                       % env.initramfs)
        elif os.path.getsize(env.initramfs) != 8 * 1024 * 1024:
            out.append("initramfs must be exactly 8MiB (fixed-slot contract)")
    if tag == "busybox":
        if not os.path.exists(env.busybox):
            out.append("missing musl busybox %s (tools/build-busybox.sh)"
                       % env.busybox)
    if tag == "yocto":
        poky = os.path.join(env.work, "poky")
        build = os.path.join(env.work, "yocto-build")
        if not os.path.exists(os.path.join(poky, "bitbake")):
            out.append("missing poky/bitbake (scripts/setup-yocto.sh)")
        if not os.path.exists(os.path.join(build, "conf", "local.conf")):
            out.append("missing yocto build tree (scripts/setup-yocto.sh)")
    if tag == "docker":
        for p in ("docker/Dockerfile.build", "scripts/docker-build.sh",
                  "scripts/make-release.sh"):
            if not os.path.exists(os.path.join(env.repo, p)):
                out.append("missing %s" % p)
    return out


def which_artifacts(gate_artifacts) -> list[str]:
    return sorted(set(gate_artifacts) - {"elf", "none"})