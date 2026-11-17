# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Julian Chu
"""Fixture definitions: one fresh world per gate or scenario."""

from __future__ import annotations

from .artifacts import Env
from . import recipes


class Fixture:
    key = "base"
    artifacts: tuple[str, ...] = ("elf",)
    with_linux = True

    def qemu_args(self, env: Env, sock: str, gdb_port: int | None = None):
        return recipes.qemu_args(env, sock, with_linux=self.with_linux,
                                 gdb_port=gdb_port)

    def prepare(self, s, env: Env) -> None:
        recipes.boot_host(s, env)


class HostFixture(Fixture):
    key = "host"


class VM0Fixture(Fixture):
    key = "vm0"

    def prepare(self, s, env: Env) -> None:
        recipes.boot_host(s, env)
        recipes.vm0_ready(s, env, attach=True)


class VM0BootFixture(Fixture):
    """Host only; the boot-stream scenario issues vmcreate/vmrun itself."""
    key = "vm0boot"


class VM0ReadyFixture(Fixture):
    """VM0 booted through init/services, console still on the host."""

    key = "vm0ready"

    def prepare(self, s, env: Env) -> None:
        recipes.boot_host(s, env)
        recipes.vm0_ready(s, env, attach=False)


class VM1Fixture(Fixture):
    key = "vm1"

    def prepare(self, s, env: Env) -> None:
        recipes.boot_host(s, env)
        recipes.vm1_ready(s, env)


class VM2Fixture(Fixture):
    key = "vm2"
    artifacts = ("elf", "linux")

    def prepare(self, s, env: Env) -> None:
        recipes.boot_host(s, env)
        recipes.vm2_ready(s, env)


class VM2BootFixture(Fixture):
    """VM2 started through the boot window (no VM0 dependency)."""

    key = "vm2boot"
    artifacts = ("elf", "linux")

    def prepare(self, s, env: Env) -> None:
        recipes.boot_host(s, env)
        p = s.mark()
        s.send("vmcreate 2\n")
        s.expect("vm2: CREATED", 45.0, start=p)
        p = s.mark()
        s.send("vmrun 2\n")
        s.expect("Booting Linux on physical CPU", 270.0, start=p)


class VM2Boot0Fixture(Fixture):
    """VM0 running (not attached) plus VM2 in its boot window."""

    key = "vm2boot0"
    artifacts = ("elf", "linux")

    def prepare(self, s, env: Env) -> None:
        recipes.boot_host(s, env)
        recipes.vm0_ready(s, env, attach=False)
        p = s.mark()
        s.send("vmcreate 2\n")
        s.expect("vm2: CREATED", 45.0, start=p)
        p = s.mark()
        s.send("vmrun 2\n")
        s.expect("Booting Linux on physical CPU", 270.0, start=p)


class DualFixture(Fixture):
    """VM0 attached then released, VM1 booted; console on the host."""

    key = "dual"

    def prepare(self, s, env: Env) -> None:
        recipes.boot_host(s, env)
        recipes.vm0_ready(s, env, attach=True)
        p = s.mark()
        s.send("host\n")
        s.expect("console -> host", 30.0, start=p)
        recipes.vm1_boot(s, env)


class MultiFixture(Fixture):
    key = "multi"
    artifacts = ("elf", "linux")

    def prepare(self, s, env: Env) -> None:
        recipes.boot_host(s, env)
        recipes.vm0_ready(s, env, attach=True)
        recipes.escape_to_host(s)
        recipes.vm1_ready(s, env)
        recipes.escape_to_host(s)
        recipes.vm2_ready(s, env)


FIXTURES: dict[str, Fixture] = {
    "host": HostFixture(),
    "vm0": VM0Fixture(),
    "vm0boot": VM0BootFixture(),
    "vm0ready": VM0ReadyFixture(),
    "vm1": VM1Fixture(),
    "dual": DualFixture(),
    "vm2": VM2Fixture(),
    "vm2boot": VM2BootFixture(),
    "vm2boot0": VM2Boot0Fixture(),
    "multi": MultiFixture(),
}


def get(key: str | None) -> Fixture | None:
    if key is None:
        return None
    return FIXTURES[key]


def fixture_artifacts(key: str | None) -> tuple[str, ...]:
    f = get(key)
    return f.artifacts if f is not None else ("none",)