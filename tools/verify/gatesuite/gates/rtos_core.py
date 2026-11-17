# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Julian Chu
"""VM0 RTOS core gates 19..44 (fresh VM0 fixture; mutex is a scenario)."""

from __future__ import annotations

import re
import socket
import time

from .. import recipes
from ..registry import gate, scenario, scenario_gate


def _send(ctx, line: str, *, guest: bool = False) -> int:
    p = ctx.mark()
    (ctx.s.send_guest if guest else ctx.s.send)(line + "\n")
    return p


@gate(19, "RX wake chain: push -> irq -> shell task READY -> prompt",
      fixture="vm0ready", timeout_s=90.0)
def gate_19(ctx):
    recipes.attach_vm(ctx.s, 0, "\r\nrtos> ", pre_sleep_s=2.5,
                      wake_retries=6, timeout_s=25.0)


@gate(20, "VM0 (rtos) selftest OK (x19/x30 live across 100 MMIO traps)",
      fixture="vm0", timeout_s=90.0)
def gate_20(ctx):
    p = _send(ctx, "selftest")
    ctx.s.expect("selftest OK", 60.0, start=p)
    ctx.s.expect("rtos> ", 30.0, start=p)


@gate(21, "ps: shell/heartbeat/idle rows (task table live)",
      fixture="vm0", timeout_s=60.0)
def gate_21(ctx):
    p = _send(ctx, "ps")
    ctx.s.expect("rtos> ", 30.0, start=p)
    for row in (b"shell", b"heartbeat", b"idle"):
        if row not in ctx.s.buf[p:]:
            raise AssertionError("ps missing row %r: %r"
                                 % (row, ctx.s.buf[p:][-200:]))


@gate(22, "spawn gate16 -> born, greeted, exited to pool",
      fixture="vm0", timeout_s=75.0)
def gate_22(ctx):
    p = _send(ctx, "spawn gate16", guest=True)
    ctx.s.expect("gate16: hello from task", 45.0, start=p)
    ctx.s.expect("rtos> ", 30.0, start=p)


@gate(23, "fp self-proof: 1e6 double sum exact (SIMD frame across VM/task switches)",
      fixture="vm0", timeout_s=210.0)
def gate_23(ctx):
    p = _send(ctx, "fp")
    ctx.s.expect("rtos> ", 30.0, start=p)
    ctx.s.expect("fp OK: 500000500000", 180.0, start=p)


@gate(24, "shell sleep 3s: heartbeat interleaved (task-level concurrency)",
      fixture="vm0", timeout_s=90.0)
def gate_24(ctx):
    p = _send(ctx, "sleep 3")
    ctx.s.expect("sleeping...", 30.0, start=p)
    ctx.s.expect("slept (rtos sleep)", 60.0, start=p)
    end = ctx.s.buf.index(b"slept (rtos sleep)", p)
    if b"rtos: alive" not in ctx.s.buf[p:end]:
        raise AssertionError("no heartbeat during shell sleep: %r"
                             % ctx.s.buf[p:end][-200:])


@gate(25, "HVC#2 yield -> PAUSED -> vmrun resume -> '[vm0] resumed' (task ctx intact)",
      fixture="vm0", timeout_s=90.0)
def gate_25(ctx):
    p = _send(ctx, "yield")
    ctx.s.expect("yielding...", 30.0, start=p)
    ctx.s.expect("yielded -> PAUSED", 30.0, start=p)
    pos = ctx.mark()
    ctx.s.send("vmrun 0\n")
    ctx.s.expect("[vm0] resumed", 45.0, start=pos)


@gate(26, "HVC#4 handback + vmstat readable", fixture="vm0",
      timeout_s=90.0)
def gate_26(ctx):
    p = _send(ctx, "host")
    ctx.s.expect("console -> host", 30.0, start=p)
    p = _send(ctx, "vmstat")
    ctx.s.expect("preempts: ", 45.0, start=p)
    if not re.search(rb"preempts: (\d+)", ctx.s.buf[p:]):
        raise AssertionError("vmstat unparseable: %r" % ctx.s.buf[p:][-200:])


@gate(27, "semdemo: bounded ring 8/8 (sem pair, blocking ping-pong)",
      fixture="vm0", timeout_s=90.0)
def gate_27(ctx):
    p = _send(ctx, "semdemo")
    ctx.s.expect("semdemo: ring done (8/8)", 60.0, start=p)
    if ctx.s.buf[p:].count(b"semdemo: p") != 8 or \
            ctx.s.buf[p:].count(b"semdemo: c") != 8:
        raise AssertionError("semdemo item counts wrong (p=%d c=%d): %r"
                             % (ctx.s.buf[p:].count(b"semdemo: p"),
                                ctx.s.buf[p:].count(b"semdemo: c"),
                                ctx.s.buf[p:][-200:]))


scenario_gate(28, "ps mid-hold: waiter BLOCK on mutex, contender SLEEP (live state)",
              "mutex-hold")
scenario_gate(29, "mutex: block-wake order + recursion + non-owner denial",
              "mutex-hold")


@scenario("mutex-hold", "vm0", (28, 29))
def scenario_mutex(ctx):
    s = ctx.s
    p_md = ctx.mark()
    s.send("mutexdemo\n")
    s.expect("mutex: contender locked", 30.0, start=p_md)

    p_ps = p_md
    for attempt in range(2):
        p_ps = ctx.mark()
        s.send("ps\n")
        s.expect("rtos> ", 30.0, start=p_ps)
        if 28 not in ctx.selected:
            break
        w = s.buf[p_ps:]
        if b"waiter prio 8  BLOCK" in w and b"contender prio 6  SLEEP" in w:
            break
        if attempt == 0:
            time.sleep(0.3)

    def check_28():
        w = s.buf[p_ps:]
        if b"waiter prio 8  BLOCK" not in w or \
                b"contender prio 6  SLEEP" not in w:
            raise AssertionError("ps mid-hold rows missing (waiter BLOCK/"
                                 "contender SLEEP): %r" % w[-300:])

    ctx.check(28, "ps mid-hold: waiter BLOCK on mutex, contender SLEEP (live state)",
              check_28)

    def check_29():
        s.expect("mutex: waiter done", 60.0, start=p_md)
        w = s.buf[p_md:]
        i = w.index(b"mutex: contender unlocking")
        if w.index(b"mutex: waiter locked (after block)", i) <= i:
            raise AssertionError("waiter locked before contender unlock: %r"
                                 % w[-300:])
        if b"mutex: give denied (not owner)" not in w or \
                b"mutex: recursive take 2 ok" not in w:
            raise AssertionError("mutex denial/recursion needles missing: %r"
                                 % w[-300:])

    ctx.check(29, "mutex: block-wake order + recursion + non-owner denial",
              check_29)


@gate(30, "pidemo: PI starvation reversal (low 9->6->9, high got lock)",
      fixture="vm0", timeout_s=120.0)
def gate_30(ctx):
    p = _send(ctx, "pidemo")
    ctx.s.expect("pi: low restored prio 9", 60.0, start=p)
    ctx.s.expect("pi: mid done", 45.0, start=p)
    w = ctx.s.buf[p:]
    a = w.index(b"pi: mid spinning")
    if w.index(b"pi: low done (boosted)") < a:
        raise AssertionError("low done before mid spin (PI proof broken): %r"
                             % w[-300:])
    for needle in (b"pi: low in CS at prio 9",
                   b"pi: low running at prio 6 (boosted)",
                   b"pi: high got lock (after boost)"):
        if needle not in w:
            raise AssertionError("pidemo needle missing %r: %r"
                                 % (needle, w[-300:]))


@gate(31, "fpdual: dual FP tasks both exact (per-task SIMD frame)",
      fixture="vm0", timeout_s=120.0)
def gate_31(ctx):
    p = _send(ctx, "fpdual")
    ctx.s.expect("fpdual: B OK: 250000250000", 90.0, start=p)
    if b"fpdual: A OK: 500000500000" not in ctx.s.buf[p:]:
        raise AssertionError("fpdual A missing: %r" % ctx.s.buf[p:][-300:])


@gate(32, "eventdemo: AND needs both bits, OR wakes on single",
      fixture="vm0", timeout_s=120.0)
def gate_32(ctx):
    p = _send(ctx, "eventdemo")
    ctx.s.expect("evt: waiter fired flags=0x5", 90.0, start=p)
    ctx.s.expect("evt: OR fired flags=0x8", 45.0, start=p)
    w = ctx.s.buf[p:]
    a = w.index(b"evt: waiter fired flags=0x5")
    if w.index(b"evt: A set (waiter stays)") > a:
        raise AssertionError("AND waiter woke before both bits: %r" % w[-300:])


@gate(33, "queuedemo: 16 msgs ring + full-blocking handoff",
      fixture="vm0", timeout_s=120.0)
def gate_33(ctx):
    p = _send(ctx, "queuedemo")
    ctx.s.expect("q: ring full, sender blocked", 60.0, start=p)
    ctx.s.expect("q: done 16/16", 60.0, start=p)
    w = ctx.s.buf[p:]
    if w.count(b"q: sent ") < 16 or w.count(b"q: got ") < 16:
        raise AssertionError("queue 16-msg chain incomplete: %r" % w[-300:])


@gate(34, "fpstat: lazy FP saves << switches", fixture="vm0",
      timeout_s=150.0)
def gate_34(ctx):
    # A fresh boot has no FP task context to save; generate one first.
    p = _send(ctx, "fpdual")
    ctx.s.expect("fpdual: B OK: 250000250000", 90.0, start=p)
    p = _send(ctx, "fpstat")
    ctx.s.expect("fp-saves", 45.0, start=p)
    m = re.search(rb"lazy: (\d+) switches, (\d+) fp-saves", ctx.s.buf[p:])
    if not m:
        raise AssertionError("fpstat format: %r" % ctx.s.buf[p:][-120:])
    n_sw, n_sv = int(m.group(1)), int(m.group(2))
    if not (0 < n_sv < n_sw):
        raise AssertionError("fpstat M<<N broken: %d saves / %d switches"
                             % (n_sv, n_sw))


@gate(35, "RTOS secondary online (smpstat), frame1 en=0x08000001",
      fixture="vm0", timeout_s=60.0)
def gate_35(ctx):
    p = _send(ctx, "smpstat")
    ctx.s.expect("smp: cpu1 gicr1_en=", 30.0, start=p)
    m = re.search(rb"smp: cpu1 gicr1_en=(\d+)", ctx.s.buf[p:])
    if not m or int(m.group(1)) != 0x08000001:
        raise AssertionError(
            "secondary frame1 ISENABLER0 readback bad: %r (boot line seen: %s)"
            % (ctx.s.buf[p:][-300:],
               b"cpu1: online (rtos psci)" in ctx.s.buf))


@gate(36, "smpdemo: dual-core counters interleave, lines intact",
      fixture="vm0", timeout_s=90.0)
def gate_36(ctx):
    p = _send(ctx, "smpdemo")
    ctx.s.expect("cpu0: done=", 60.0, start=p)
    ctx.s.expect("cpu1: done=", 60.0, start=p)
    w = ctx.s.buf[p:]
    c0, c1 = w.count(b"cpu0: n="), w.count(b"cpu1: n=")
    m0 = len(re.findall(rb"cpu0: n=(\d+)", w))
    m1 = len(re.findall(rb"cpu1: n=(\d+)", w))
    if c1 < 2 or c0 < 2 or c1 != m1 or c0 != m0:
        raise AssertionError("smpdemo interleave/torn (cpu0 %d/%d, cpu1 %d/%d): %r"
                             % (c0, m0, c1, m1, w[:400]))


@gate(37, "ipidemo: SGI roundtrips + 3 cross-core wakes, window quiet",
      fixture="vm0", timeout_s=150.0)
def gate_37(ctx):
    p = _send(ctx, "ipidemo")
    ctx.s.expect("ipi: rx0=", 90.0, start=p)
    ctx.s.expect("ipi: sleeper done", 120.0, start=p)
    w = ctx.s.buf[p:]
    m = re.search(rb"ipi: rx0=(\d+) roundtrips", w)
    if not m or int(m.group(1)) < 20:
        raise AssertionError("ipi roundtrips < 20: %r" % w[:300])
    if w.count(b"ipi: cpu1 awake") != 3:
        raise AssertionError("sleeper wakes != 3: %r" % w[:400])
    if re.search(rb"rtos: alive", w):
        raise AssertionError("spontaneous heartbeat inside ipidemo window: %r"
                             % w[:200])


@gate(38, "fpsmp: per-CPU FP frames both exact", fixture="vm0",
      timeout_s=150.0)
def gate_38(ctx):
    p = _send(ctx, "fpsmp")
    ctx.s.expect("fpsmp: cpu0 A OK: 500000500000", 120.0, start=p)
    ctx.s.expect("fpsmp: cpu1 B OK: 250000250000", 120.0, start=p)


@gate(39, "no-regression with secondary online: ps ok, fpstat grows",
      fixture="vm0", timeout_s=120.0)
def gate_39(ctx):
    p = _send(ctx, "ps")
    ctx.s.expect("rtos> ", 45.0, start=p)
    if b"shell prio 5" not in ctx.s.buf[p:] or \
            b"heartbeat" not in ctx.s.buf[p:]:
        raise AssertionError("ps regression with secondary online: %r"
                             % ctx.s.buf[p:][-300:])
    p = _send(ctx, "fpstat")
    ctx.s.expect("fp-saves", 45.0, start=p)
    m = re.search(rb"lazy: (\d+) switches, (\d+) fp-saves", ctx.s.buf[p:])
    if not m:
        raise AssertionError("fpstat first reading format: %r"
                             % ctx.s.buf[p:][-120:])
    n1 = int(m.group(1))
    # Self-contained growth proof: the shell sleep generates switches.
    p = _send(ctx, "sleep 1")
    ctx.s.expect("slept (rtos sleep)", 30.0, start=p)
    p = _send(ctx, "fpstat")
    ctx.s.expect("fp-saves", 45.0, start=p)
    m = re.search(rb"lazy: (\d+) switches, (\d+) fp-saves", ctx.s.buf[p:])
    if not m:
        raise AssertionError("fpstat second reading format: %r"
                             % ctx.s.buf[p:][-120:])
    n2, s2 = int(m.group(1)), int(m.group(2))
    if n2 <= n1 or s2 > n2:
        raise AssertionError("fpstat regression: %d -> %d (saves %d)"
                             % (n1, n2, s2))


@gate(40, "tmodemo: timeout face full arc (PI 14->9->14)",
      fixture="vm0", timeout_s=180.0)
def gate_40(ctx):
    p = _send(ctx, "tmodemo")
    ctx.s.expect("tmo: a done", 135.0, start=p)
    w = ctx.s.buf[p:]
    for needle in (b"tmo: sem nowait ret=-2", b"tmo: q send ret=-2",
                   b"tmo: sem timeout ret=-2", b"tmo: sem got ret=0",
                   b"tmo: mutex timeout ret=-2", b"tmo: q drain 4 ret=0",
                   b"tmo: q recv timeout ret=-2", b"tmo: b done"):
        if needle not in w:
            raise AssertionError("tmodemo needle missing: %r" % needle)
    i9 = w.find(b"tmo: c prio 9")
    i14 = w.find(b"tmo: mutex timeout")
    i14b = w.find(b"tmo: c prio 14", i14)
    if not (0 <= i9 < i14 < i14b):
        raise AssertionError("PI arc order broken (boost->timeout->recover): %r"
                             % w[:200])


@gate(41, "stkdemo: deepdiver margin << shell, magic ok, ps stk col",
      fixture="vm0", timeout_s=90.0)
def gate_41(ctx):
    p = _send(ctx, "stkdemo")
    ctx.s.expect("stkdemo: dived", 45.0, start=p)
    ctx.s.send("stackcheck\n")
    ctx.s.expect("stk: magic ok", 30.0, start=p)
    w = ctx.s.buf[p:]
    m = re.search(rb"stk: deepdiver margin (\d+)", w)
    m2 = re.search(rb"stk: shell margin (\d+)", w)
    if not m or not m2 or int(m.group(1)) >= int(m2.group(1)) \
            or int(m.group(1)) > 1000:
        raise AssertionError("deepdiver margin judge: %r" % w[:300])
    p2 = _send(ctx, "ps")
    ctx.s.expect("ps: task table", 30.0, start=p2)
    h = ctx.s.buf.index(b"ps: task table", p2) + len(b"ps: task table")
    ctx.s.expect(" stk ", 5.0, start=h)
    if b" stk " not in ctx.s.buf[h:]:
        raise AssertionError("ps missing stk column: %r"
                             % ctx.s.buf[h:][-200:])


@gate(42, "swtdemo: oneshot x1 + periodic x3 self-stop + cancel",
      fixture="vm0", timeout_s=90.0)
def gate_42(ctx):
    p = _send(ctx, "swtdemo")
    ctx.s.expect("swt: done", 75.0, start=p)
    end = time.time() + 1.5
    while time.time() < end:
        try:
            ctx.s.recv(0.2)
        except socket.timeout:
            pass
    w = ctx.s.buf[p:]
    for needle in (b"swt: periodic 1", b"swt: periodic 2",
                   b"swt: periodic 3", b"swt: periodic stopped",
                   b"swt: oneshot fired", b"swt: cancelled", b"swt: done"):
        if needle not in w:
            raise AssertionError("swtdemo needle missing: %r" % needle)
    if w.count(b"swt: oneshot fired") != 1:
        raise AssertionError("oneshot fired %dx: %r"
                             % (w.count(b"swt: oneshot fired"), w[:300]))
    if len(re.findall(rb"swt: periodic \d", w)) != 3:
        raise AssertionError("periodic fired %dx: %r"
                             % (len(re.findall(rb"swt: periodic \d", w)),
                                w[:400]))
    if b"swt: late" in w:
        raise AssertionError("cancelled timer fired: %r" % w[-200:])
    if not (w.find(b"swt: periodic 1") < w.find(b"swt: cancelled")
            < w.find(b"swt: oneshot fired")
            < w.find(b"swt: periodic 3")
            < w.find(b"swt: periodic stopped")):
        raise AssertionError("swtdemo order broken: %r" % w[:300])


@gate(43, "irqdemo: registry chain, uart slot restored, shell alive",
      fixture="vm0", timeout_s=75.0)
def gate_43(ctx):
    p = _send(ctx, "irqdemo")
    time.sleep(0.1)
    ctx.s.send("x\n")
    ctx.s.expect("irqdemo: done", 45.0, start=p)
    m = re.search(rb"irq: chained (\d+)", ctx.s.buf[p:])
    if not m or int(m.group(1)) < 1:
        raise AssertionError("irq chain count < 1: %r"
                             % ctx.s.buf[p:][:300])
    p2 = _send(ctx, "ps")
    ctx.s.expect("ps: task table", 30.0, start=p2)


@gate(44, "lowdemo: guard warned once at depth, magic untouched",
      fixture="vm0", timeout_s=90.0)
def gate_44(ctx):
    p = _send(ctx, "lowdemo")
    ctx.s.expect("lowdemo: done", 60.0, start=p)
    if ctx.s.buf[p:].count(b"stk: lowmargin lowrunner") != 1:
        raise AssertionError("lowmargin warn count != 1: %r"
                             % ctx.s.buf[p:][:300])
    p2 = _send(ctx, "stackcheck")
    ctx.s.expect("stk: magic ok", 30.0, start=p2)