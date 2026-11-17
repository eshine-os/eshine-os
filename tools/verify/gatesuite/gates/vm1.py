# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Julian Chu
"""VM1 bare-metal and dual-VM gates 45..61.

Stateful families stay in scenarios; everything else gets a fresh world
from the `dual` fixture (VM0 running, VM1 booted, console on the host).
Retired: [58] folded into [59], [61] folded into [57].
"""

from __future__ import annotations

import os
import re

from .. import recipes
from ..registry import gate, scenario, scenario_gate


def _attach(ctx, vm_id: int, prompt: str, *, pre_sleep: float = 0.0):
    recipes.attach_vm(ctx.s, vm_id, prompt, pre_sleep_s=pre_sleep,
                      wake_retries=6, timeout_s=60.0)


def _release_host(ctx) -> None:
    p = ctx.mark()
    ctx.s.send("host\n")
    ctx.s.expect("console -> host", 30.0, start=p)


def _escape(ctx) -> None:
    p = ctx.mark()
    ctx.s.send(b"\x1d")
    ctx.s.expect("console -> host (escape)", 30.0, start=p)


@gate(45, "vmrun 1 async -> bare-metal banner via vuart ([vm1] prefix)",
      fixture="host", timeout_s=120.0)
def gate_45(ctx):
    s = ctx.s
    p = ctx.mark()
    s.send("vmcreate 1\n")
    s.expect("vm1: CREATED", 30.0, start=p)
    p = ctx.mark()
    s.send("vmrun 1\n")
    s.expect("eshine> ", 30.0, start=p)
    s.expect("[vm1] eshine-guest VM1 @EL1", 60.0, start=p)
    _attach(ctx, 1, "\nvm1> ")


@gate(46, "both RUNNING (vmstat)", fixture="dual", timeout_s=90.0)
def gate_46(ctx):
    s = ctx.s
    _attach(ctx, 1, "\nvm1> ")
    _release_host(ctx)
    p = ctx.mark()
    s.send("vmstat\n")
    s.expect("[0] VM_RUNNING", 30.0, start=p)
    s.expect("[1] VM_RUNNING", 30.0, start=p)
    s.expect("preempts: ", 30.0, start=p)
    if not re.search(rb"preempts: (\d+)", s.buf[p:]):
        raise AssertionError("vmstat preempts line missing: %r"
                             % s.buf[p:][-120:])


@gate(47, "vm0 (rtos) echo double-echo ok (foreground)",
      fixture="dual", timeout_s=90.0)
def gate_47(ctx):
    s = ctx.s
    _attach(ctx, 0, "\r\nrtos> ", pre_sleep=0.0)
    p2 = ctx.mark()
    from .. import serial
    serial.token_echo(s, "GOKA%d" % os.getpid())
    if re.search(rb"rtos: alive", s.buf[p2:]):
        raise AssertionError("spontaneous heartbeat at interactive prompt: %r"
                             % s.buf[p2:][-200:])
    _release_host(ctx)


@gate(48, "virtio write/read + completion IRQ (3-desc chain -> vSPI 35)",
      fixture="dual", timeout_s=120.0)
def gate_48(ctx):
    s = ctx.s
    _attach(ctx, 1, "\nvm1> ")
    p = ctx.mark()
    s.send("dwrite\n")
    s.expect("blk wrote", 45.0, start=p)
    p = ctx.mark()
    s.send("dread\n")
    s.expect("blk read: ESHINE-BLK-persist", 45.0, start=p)
    _release_host(ctx)


@gate(49, "vm1 echo double-echo ok (both alive, interleaved)",
      fixture="dual", timeout_s=90.0)
def gate_49(ctx):
    from .. import serial
    _attach(ctx, 1, "\nvm1> ")
    serial.token_echo(ctx.s, "GOKB%d" % os.getpid())
    _release_host(ctx)


@gate(50, "spin 3G survived quantum preemptions (frame migration intact)",
      fixture="dual", timeout_s=240.0)
def gate_50(ctx):
    s = ctx.s
    p = ctx.mark()
    s.send("vmstat\n")
    s.expect("preempts: ", 30.0, start=p)
    m = re.search(rb"preempts: (\d+)", s.buf[p:])
    if not m:
        raise AssertionError("pre-spin vmstat unparseable: %r"
                             % s.buf[p:][-120:])
    pe0 = int(m.group(1))
    _attach(ctx, 1, "\nvm1> ")
    p = ctx.mark()
    s.send("spin\n")
    s.expect("spinning...", 30.0, start=p)
    s.expect("spun 3G iters (preempted ok)", 180.0, start=p)
    _release_host(ctx)
    p = ctx.mark()
    s.send("vmstat\n")
    s.expect("preempts: ", 30.0, start=p)
    m = re.search(rb"preempts: (\d+)", s.buf[p:])
    if not m:
        raise AssertionError("post-spin vmstat unparseable: %r"
                             % s.buf[p:][-120:])
    pe1 = int(m.group(1))
    if pe1 - pe0 < 2:
        raise AssertionError("spin crossed %d quantum(s) (pe0=%d pe1=%d)"
                             % (pe1 - pe0, pe0, pe1))


scenario_gate(51, "vm1 WAIT -> host alive, rtos running (fairness)",
              "vm1-wait")
scenario_gate(52, "vm1 woke in background rotation (WOKE->pump inject)",
              "vm1-wait")


@scenario("vm1-wait", "dual", (51, 52))
def scenario_vm1_wait(ctx):
    s = ctx.s
    _attach(ctx, 1, "\nvm1> ")
    p = ctx.mark()
    s.send("sleep\n")
    s.expect("sleeping...", 30.0, start=p)
    _escape(ctx)

    def check_51():
        from .. import serial
        serial.token_echo(s, "FAIR%d" % os.getpid())

    ctx.check(51, "vm1 WAIT -> host alive, rtos running (fairness)", check_51)

    def check_52():
        s.expect("[vm1] slept (vtimer irq)", 60.0, start=p)

    ctx.check(52, "vm1 woke in background rotation (WOKE->pump inject)",
              check_52)


scenario_gate(53, "dual sleep: rtos 2s beats bare-metal 4s; per-VM vtimer ctx",
              "dual-sleep")
scenario_gate(54, "both shells alive after dual sleep (rtos echo ok)",
              "dual-sleep")


@scenario("dual-sleep", "dual", (53, 54))
def scenario_dual_sleep(ctx):
    s = ctx.s
    _attach(ctx, 1, "\nvm1> ")
    p_sleep4 = ctx.mark()
    s.send("sleep 4\n")
    s.expect("sleeping...", 30.0, start=p_sleep4)
    _escape(ctx)
    _attach(ctx, 0, "\r\nrtos> ", pre_sleep=0.0)
    p = ctx.mark()
    s.send("sleep 2\n")
    s.expect("sleeping...", 30.0, start=p)
    _escape(ctx)

    def check_53():
        s.expect("[vm0] slept (rtos sleep)", 45.0, start=p)
        p_ord = s.buf.index(b"[vm0] slept (rtos sleep)") + 1
        s.expect("[vm1] slept (vtimer irq)", 45.0, start=p_ord)

    ctx.check(53, "dual sleep: rtos tick-sleep (2s) beats bare-metal "
              "one-shot (4s); per-VM vtimer ctx", check_53)

    def check_54():
        _attach(ctx, 0, "\r\nrtos> ", pre_sleep=0.0)
        from .. import serial
        serial.token_echo(s, "DUAL%d" % os.getpid())
        _release_host(ctx)

    ctx.check(54, "both shells alive after dual sleep (rtos echo ok)",
              check_54)


@gate(55, "virtio persistence: data survives destroy+recreate",
      fixture="dual", timeout_s=180.0)
def gate_55(ctx):
    s = ctx.s
    _attach(ctx, 1, "\nvm1> ")
    p = ctx.mark()
    s.send("dwrite\n")
    s.expect("blk wrote", 45.0, start=p)
    p = ctx.mark()
    s.send("exit\n")
    s.expect("vm1: VM_DEAD", 45.0, start=p)
    p = ctx.mark()
    s.send("vmcreate 1\n")
    s.expect("vm1: CREATED", 30.0, start=p)
    s.send("vmrun 1\n")
    s.expect("[vm1] eshine-guest VM1 @EL1", 60.0, start=p)
    _attach(ctx, 1, "\nvm1> ")
    p = ctx.mark()
    s.send("dread\n")
    s.expect("blk read: ESHINE-BLK-persist", 45.0, start=p)


@gate(56, "vm1 fault isolated: VM_DEAD, host alive", fixture="dual",
      timeout_s=90.0)
def gate_56(ctx):
    s = ctx.s
    _attach(ctx, 1, "\nvm1> ")
    p = ctx.mark()
    s.send("fault\n")
    s.expect("fault: ESR=0x", 30.0, start=p)
    s.expect("vm1: faulted -> VM_DEAD", 30.0, start=p)


@gate(57, "vmls: rtos RUNNING, bare-metal DEAD; vmstat sane after teardown",
      fixture="dual", timeout_s=120.0)
def gate_57(ctx):
    s = ctx.s
    _attach(ctx, 1, "\nvm1> ")
    p = ctx.mark()
    s.send("fault\n")
    s.expect("vm1: faulted -> VM_DEAD", 30.0, start=p)
    p = ctx.mark()
    s.send("vmls\n")
    s.expect("[0] VM_RUNNING", 30.0, start=p)
    s.expect("[1] VM_DEAD", 30.0, start=p)
    s.expect("eshine> ", 30.0, start=p)
    p = ctx.mark()
    s.send("vmstat\n")
    s.expect("[1] VM_DEAD", 30.0, start=p)


gate(58, "host shell intact after bare-metal lifecycle", fixture="host",
     status="retired",
     reason="folded into [59] host token+tick")(lambda ctx: None)


@gate(59, "post-guest tick grows (HCR_EL2_HOST/DAIF restore)",
      fixture="dual", timeout_s=120.0)
def gate_59(ctx):
    from .. import serial
    s = ctx.s
    _attach(ctx, 1, "\nvm1> ")
    p = ctx.mark()
    s.send("fault\n")
    s.expect("vm1: faulted -> VM_DEAD", 30.0, start=p)
    # [58] folded: the host token proves the shell survived the lifecycle.
    serial.token_echo(s, "ESHINEOK%d" % (os.getpid() + 1))
    serial.tick_pair(s, "post-guest tick")


@gate(60, "vmkill deny path ok (default-deny)", fixture="dual",
      timeout_s=90.0)
def gate_60(ctx):
    s = ctx.s
    _attach(ctx, 1, "\nvm1> ")
    p = ctx.mark()
    s.send("exit\n")
    s.expect("vm1: VM_DEAD", 45.0, start=p)
    p = ctx.mark()
    s.send("vmkill 1\n")
    s.expect("kill denied", 30.0, start=p)


gate(61, "vmstat still sane after teardown", fixture="host",
     status="retired", reason="folded into [57]")(lambda ctx: None)