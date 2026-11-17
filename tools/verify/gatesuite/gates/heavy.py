# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Julian Chu
"""Heavy gates 133/177/178/180: local/release only.

These gates need a Yocto tree (`scripts/setup-yocto.sh`) or inspect the
container/release surface. They are excluded from the cloud green suite
by status `heavy` and run with `make verify-heavy` / `--suite all`.

[133], [177] and [178] share one `scripts/yocto-build.sh` invocation per
process (sstate fast path; the script prints both DEPLOY= and IMAGE=).
Each gate still builds the products it needs when selected alone, so
selection independence is preserved without triple-building in a full
heavy run.
"""

from __future__ import annotations

import hashlib
import json
import os
import re
import socket
import subprocess
import time

from ..registry import gate

_BUILD_CACHE: dict[str, str] = {}


def _build(ctx) -> dict[str, str]:
    """Run the Yocto build script once; return {DEPLOY, IMAGE}."""
    if _BUILD_CACHE:
        return _BUILD_CACHE
    env = ctx.env
    rb = subprocess.run(["bash", "scripts/yocto-build.sh"],
                        capture_output=True, text=True, cwd=env.repo,
                        timeout=1800)
    if rb.returncode != 0:
        raise AssertionError("yocto build failed: %r / %r"
                             % (rb.stdout[-400:], rb.stderr[-400:]))
    out: dict[str, str] = {}
    for key in ("DEPLOY", "IMAGE"):
        m = re.search(r"^%s=(\S+)$" % key, rb.stdout, re.M)
        if m:
            out[key] = m.group(1)
    if "DEPLOY" not in out:
        raise AssertionError("yocto build output missing DEPLOY=: %r"
                             % rb.stdout[-400:])
    _BUILD_CACHE.update(out)
    return out


def _boot_product(ctx, argv: list[str], sock: str) -> None:
    """Boot a product QEMU/run.sh, drive it to a musl hello, then stop."""
    if os.path.exists(sock):
        os.unlink(sock)
    proc = subprocess.Popen(argv, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL,
                            start_new_session=True)
    try:
        deadline = time.time() + 90
        while not os.path.exists(sock):
            if time.time() > deadline:
                raise AssertionError("product serial socket never appeared")
            time.sleep(0.1)
        sx = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sx.connect(sock)
        sx.settimeout(1.0)
        buf = b""

        def read(needle: bytes, timeout_s: float) -> None:
            nonlocal buf
            deadline = time.time() + timeout_s
            while needle not in buf:
                if time.time() > deadline:
                    raise AssertionError(
                        "product boot: %r not found; tail=%r"
                        % (needle, buf[-250:]))
                try:
                    chunk = sx.recv(4096)
                    if chunk:
                        buf += chunk
                except socket.timeout:
                    continue

        try:
            read(b"eshine> ", 30)
            sx.sendall(b"vmcreate 0\n")
            read(b"vm0: CREATED", 15)
            sx.sendall(b"vmrun 0\n")
            read(b"rtos: up", 30)
            sx.sendall(b"vmconsole 0\n")
            read(b"console -> vm", 10)
            sx.sendall(b"\n")
            read(b"rtos> ", 20)
            sx.sendall(b"run /bin/hello\n")
            read(b"musl: hello from /bin/hello", 60)
            read(b"run: exited code=0", 15)
        finally:
            sx.close()
    finally:
        try:
            os.killpg(os.getpgid(proc.pid), 15)
        except ProcessLookupError:
            pass
        try:
            proc.wait(5)
        except subprocess.TimeoutExpired:
            try:
                os.killpg(os.getpgid(proc.pid), 9)
            except ProcessLookupError:
                pass
            proc.wait(5)


@gate(133, "yocto: bitbake eshine-rtos -> deployed artifacts boot "
      "(musl run on the build-system product)",
      fixture=None, suites=("heavy",), status="heavy",
      artifacts=("yocto",), timeout_s=1800.0)
def gate_133(ctx):
    env = ctx.env
    deploy = _build(ctx)["DEPLOY"]
    for name in ("eshine.elf", "rtos.bin", "guest2.bin"):
        if not os.path.isfile(os.path.join(deploy, name)):
            raise AssertionError("deploy artifact missing: %s/%s"
                                 % (deploy, name))
    sock = os.path.join(env.run_dir, "yocto-%d.sock" % os.getpid())
    argv = [
        env.qemu,
        "-M", "virt,virtualization=on,gic-version=3",
        "-cpu", "cortex-a57", "-m", "512", "-smp", "4",
        "-display", "none", "-monitor", "none",
        "-kernel", os.path.join(deploy, "eshine.elf"),
        "-device", "loader,file=%s,addr=0x50000000,force-raw=on"
        % os.path.join(deploy, "rtos.bin"),
        "-device", "loader,file=%s,addr=0x51000000,force-raw=on"
        % os.path.join(deploy, "guest2.bin"),
        "-chardev", "socket,id=ser0,path=%s,server=on,wait=on" % sock,
        "-serial", "chardev:ser0",
    ]
    _boot_product(ctx, argv, sock)


@gate(177, "yocto image aggregate: boot/ 4 artifacts + run.sh + "
      "manifest.json (sha256 ledger verified)",
      fixture=None, suites=("heavy",), status="heavy",
      artifacts=("yocto",), timeout_s=900.0)
def gate_177(ctx):
    imgdir = _build(ctx).get("IMAGE", "")
    if not imgdir:
        raise AssertionError("yocto build output missing IMAGE=")
    for f in ("boot/eshine.elf", "boot/rtos.bin", "boot/guest2.bin",
              "boot/eshine-ramfs.cpio", "run.sh", "manifest.json"):
        if not os.path.isfile(os.path.join(imgdir, f)):
            raise AssertionError("image missing %s" % f)
    with open(os.path.join(imgdir, "manifest.json"), encoding="utf-8") as mf:
        man = json.load(mf)
    if man.get("machine") != "eshine-qemu-a57-type1":
        raise AssertionError("manifest machine wrong: %r"
                             % man.get("machine"))
    for art in man["artifacts"]:
        path = os.path.join(imgdir, "boot", art["name"])
        with open(path, "rb") as af:
            digest = hashlib.sha256(af.read()).hexdigest()
        if digest != art["sha256"]:
            raise AssertionError("sha256 mismatch %s" % art["name"])


@gate(178, "image run.sh boots the product end-to-end (musl hello "
      "from the aggregated image) - batch D second station",
      fixture=None, suites=("heavy",), status="heavy",
      artifacts=("yocto",), timeout_s=900.0)
def gate_178(ctx):
    env = ctx.env
    imgdir = _build(ctx).get("IMAGE", "")
    if not imgdir:
        raise AssertionError("yocto build output missing IMAGE=")
    sock = os.path.join(env.run_dir, "image-%d.sock" % os.getpid())
    _boot_product(ctx, ["bash", os.path.join(imgdir, "run.sh"), sock],
                  sock)


@gate(180, "docker arc: Dockerfile.build pinned toolchain "
      "(ubuntu:24.04 base) + docker-build/make-release entry scripts + "
      "container pin audit hooks - batch E first station",
      fixture=None, suites=("heavy",), status="heavy",
      artifacts=("docker",), timeout_s=120.0)
def gate_180(ctx):
    env = ctx.env
    docker_files = ("docker/Dockerfile.build",
                    "scripts/docker-build.sh",
                    "scripts/make-release.sh")
    for p in docker_files:
        if not os.path.isfile(os.path.join(env.repo, p)):
            raise AssertionError("missing: %s" % p)
    with open(os.path.join(env.repo, "docker/Dockerfile.build"),
              encoding="utf-8") as f:
        dtext = f.read()
    for needle in ("FROM ubuntu:24.04", "gcc-aarch64-linux-gnu",
                   "binutils-aarch64-linux-gnu", "qemu-system-arm",
                   "device-tree-compiler"):
        if needle not in dtext:
            raise AssertionError("Dockerfile.build lacks %r" % needle)
    for p in docker_files[1:]:
        r = subprocess.run(["bash", "-n", os.path.join(env.repo, p)],
                           capture_output=True, timeout=30)
        if r.returncode != 0:
            raise AssertionError("bash -n failed on %s: %s"
                                 % (p, r.stderr.decode()[-200:]))
    with open(os.path.join(env.repo, "scripts/docker-build.sh"),
              encoding="utf-8") as f:
        dbsh = f.read()
    for needle in ("GCC_PIN", "pin_of", "sbom-check.py"):
        if needle not in dbsh:
            raise AssertionError("docker-build.sh lacks %r" % needle)