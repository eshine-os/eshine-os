# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Julian Chu
"""Host boot and host-shell gates 1..8 (fresh host fixture each)."""

from __future__ import annotations

import os

from .. import artifacts, serial
from ..registry import gate


@gate(1, "banner ok", fixture="host", suites=("smoke", "green"))
def gate_1(ctx):
    if artifacts.BANNER not in ctx.s.buf:
        raise AssertionError("banner missing: %r" % ctx.s.tail(400))


@gate(2, "shell prompt ok", fixture="host", suites=("smoke", "green"))
def gate_2(ctx):
    ctx.s.expect("eshine> ", 30.0)


@gate(3, "cpu1/2/3 online (psci cpu_on) — three secondary cores up",
      fixture="host", suites=("smoke", "green"))
def gate_3(ctx):
    for core in (b"cpu1", b"cpu2", b"cpu3"):
        ctx.s.expect(core + b": online (psci cpu_on)", 10.0)


@gate(4, "host token double-echo ok", fixture="host",
      suites=("smoke", "green"))
def gate_4(ctx):
    serial.token_echo(ctx.s, "ESHINEOK%d" % os.getpid())


@gate(5, "tick grows (CNTHP->GICv3->EL2 IRQ)", fixture="host",
      suites=("smoke", "green"))
def gate_5(ctx):
    serial.tick_pair(ctx.s, "host tick")


@gate(6, "vmls: empty table", fixture="host", suites=("smoke", "green"))
def gate_6(ctx):
    p = ctx.mark()
    ctx.s.send("vmls\n")
    ctx.s.expect("[0] VM_INIT", 30.0, start=p)
    ctx.s.expect("[1] VM_INIT", 30.0, start=p)


@gate(7, "vmcreate 0 -> CREATED", fixture="host",
      suites=("smoke", "green"))
def gate_7(ctx):
    p = ctx.mark()
    ctx.s.send("vmcreate 0\n")
    ctx.s.expect("vm0: CREATED", 30.0, start=p)


@gate(8, "vmconsole state gate: denied before vmrun", fixture="host",
      suites=("smoke", "green"))
def gate_8(ctx):
    p = ctx.mark()
    ctx.s.send("vmcreate 0\n")
    ctx.s.expect("vm0: CREATED", 30.0, start=p)
    p = ctx.mark()
    ctx.s.send("vmconsole 0\n")
    ctx.s.expect("vm0: denied: not running", 30.0, start=p)