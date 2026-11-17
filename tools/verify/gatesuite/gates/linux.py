# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Julian Chu
"""VM2 Linux boot and userland gates 62..77 (one boot-stream scenario)."""

from __future__ import annotations

import os
import re
import socket
import time

from .. import recipes, serial
from ..registry import scenario, scenario_gate
from ..session import FixtureError, FloodError


def _attach_setup(ctx, vm_id, prompt, *, pre_sleep=0.0):
    """Console attach is scenario setup: retry the fixture on failure."""
    try:
        recipes.attach_vm(ctx.s, vm_id, prompt, pre_sleep_s=pre_sleep,
                          wake_retries=6, timeout_s=60.0)
    except FloodError:
        raise
    except Exception as e:
        raise FixtureError("attach vm%d: %s" % (vm_id, str(e)[:1500])) from e

NAMES = {
    62: "Linux enters via stage-2 + DTB x0 (arm64 boot protocol)",
    63: "guest SMP: secondary vCPU up (Brought up 1 node, 2 CPUs)",
    64: "true parallelism: vm2 boot lines advance inside vm1 spin (2nd CPU)",
    65: "echo storm 10/10 intact through vm2 boot spam",
    66: "per-VM honest DTB: memory/cpus/timer nodes accepted",
    67: "vtimer wiring (interrupt-names virt) + tick timekeeping",
    68: "Linux gic-v3 on emulated GICD/GICR (spec word decode)",
    69: "dual-guest concurrency: hb replies advance inside Linux boot",
    70: "PL011 full driver + console handover (AMBA probe)",
    71: "virtio-mmio legacy + vblk under Linux (completion IRQ chain)",
    72: "initramfs chain: DTB initrd props -> unpack -> /init takes over",
    73: "busybox shell prompt (PID 1 userland via vmconsole)",
    74: "shell token double echo (tty echo + sh output)",
    75: "userland self-proof: cat /proc/cpuinfo -> CPU part 0xd07",
    76: "userland SMP self-proof: cpuinfo -> processor 1 (2nd vCPU)",
    77: "vm2: same musl hello binary on busybox Linux (dual-world right wing)",
}

LINUX_BOOT_IDS = (62, 63, 64, 65, 66, 67, 68, 70, 71, 72,
                  73, 74, 75, 76, 77)
for _gid in LINUX_BOOT_IDS:
    scenario_gate(_gid, NAMES[_gid], "linux-boot", suites=("green",))
scenario_gate(69, NAMES[69], "linux-vm0-concurrency", suites=("green",))


@scenario("linux-boot", "vm2boot", LINUX_BOOT_IDS)
def scenario_linux_boot(ctx):
    s = ctx.s
    if b"Booting Linux on physical CPU" not in s.buf:
        raise AssertionError("Linux boot line missing")
    p_boot = s.buf.index(b"Booting Linux on physical CPU")

    def check_62():
        if b"Booting Linux on physical CPU" not in s.buf:
            raise AssertionError("stage-2 Linux entry banner missing")

    ctx.check(62, NAMES[62], check_62)

    def check_63():
        s.expect("smp: Brought up 1 node, 2 CPUs", 360.0, start=p_boot)
        s.expect("CPU1: Booted secondary processor", 30.0, start=p_boot)

    ctx.check(63, NAMES[63], check_63)

    # VM1 setup is an action: gate 65 needs the VM1 foreground even when 64
    # is not selected.
    p = ctx.mark()
    s.send("vmcreate 1\n")
    s.expect("vm1: CREATED", 30.0, start=p)
    p = ctx.mark()
    s.send("vmrun 1\n")
    s.expect("eshine> ", 30.0, start=p)
    _attach_setup(ctx, 1, "\nvm1> ")

    if 64 in ctx.selected:
        def check_64():
            p_spin = ctx.mark()
            s.send("spin\n")
            s.expect("spinning...", 45.0, start=p_spin)
            a = s.buf.index(b"spinning...", p_spin) + len(b"spinning...")
            s.expect("spun 3G iters (preempted ok)", 180.0, start=a)
            window = s.buf[a:s.buf.index(b"spun 3G iters", a)]
            stamps = re.findall(rb"\[vm2\] \[\s*([0-9]+\.[0-9]+)\]", window)
            ts = [float(x) for x in stamps]
            if len(ts) < 3 or ts != sorted(ts):
                raise AssertionError("parallel proof failed: %d vm2 lines in spin "
                                     "window: %r" % (len(ts), window[:400]))

        ctx.check(64, NAMES[64], check_64)

    def check_65():
        p_storm = ctx.mark()
        for i in range(10):
            s.send("echo storm%03d\n" % i)
            time.sleep(0.3)
        end = time.time() + 2
        while time.time() < end:
            try:
                s.recv(0.2)
            except socket.timeout:
                pass
        w = s.buf[p_storm:]
        intact = sum(1 for i in range(10)
                     if ("storm%03d" % i).encode() in w)
        if intact != 10:
            raise AssertionError("echo storm %d/10 intact: %r"
                                 % (intact, w[:400]))

    ctx.check(65, NAMES[65], check_65)

    ctx.check(66, NAMES[66],
              lambda: s.expect("Machine model: eshine-os,vm", 90.0))

    def check_67():
        s.expect("arch_timer: cp15 timer(s) running at 62.50MHz (virt)",
                 90.0)
        s.expect("clocksource: arch_sys_counter", 90.0)

    ctx.check(67, NAMES[67], check_67)

    ctx.check(68, NAMES[68],
              lambda: s.expect("GICv3: CPU0: found redistributor 0", 90.0))

    ctx.check(70, NAMES[70],
              lambda: s.expect("printk: console [ttyAMA0] enabled", 180.0))
    ctx.check(71, NAMES[71],
              lambda: s.expect("virtio_blk virtio0: [vda] 128 512-byte "
                               "logical blocks", 270.0))

    def check_72():
        s.expect("Freeing unused kernel memory", 180.0)
        s.expect("Run /init as init process", 90.0)

    ctx.check(72, NAMES[72], check_72)

    # 73-77: release VM1 and hand the console to VM2's busybox shell.
    p = ctx.mark()
    s.send("host\n")
    s.expect("console -> host", 45.0, start=p)
    _attach_setup(ctx, 2, "~ # ")
    ctx.emit(73, NAMES[73])

    def check_74():
        p_tok = ctx.mark()
        tok = "LINUX15TK%d" % (os.getpid() + 2)
        s.send_guest(("echo " + tok + "\n").encode())
        s.expect(tok.encode(), 90.0, start=p_tok)
        p_after = s.buf.index(tok.encode(), p_tok) + len(tok)
        s.expect("~ # ", 90.0, start=p_after)
        if s.buf[p_tok:].count(tok.encode()) < 2:
            raise AssertionError("guest shell token echo incomplete: %r"
                                 % s.buf[p_tok:][-1500:])

    ctx.check(74, NAMES[74], check_74)

    def check_75():
        p_cpu = ctx.mark()
        s.send_guest(b"cat /proc/cpuinfo\n")
        s.expect("~ # ", 90.0, start=p_cpu)
        if b"CPU part\t: 0xd07" not in s.buf[p_cpu:]:
            raise AssertionError("/proc/cpuinfo missing cortex-a57 proof: %r"
                                 % s.buf[p_cpu:][-1200:])

    ctx.check(75, NAMES[75], check_75)

    def check_76():
        p_cpu = ctx.mark()
        s.send_guest(b"cat /proc/cpuinfo\n")
        s.expect("processor\t: 1", 30.0, start=p_cpu)
        s.expect("~ # ", 45.0, start=p_cpu)

    ctx.check(76, NAMES[76], check_76)

    def check_77():
        p_hello = ctx.mark()
        s.send("/bin/hello\n")
        s.expect("musl: hello from /bin/hello", 60.0, start=p_hello)
        s.expect("musl: fmt proof 6*7=42", 30.0, start=p_hello)

    ctx.check(77, NAMES[77], check_77)


@scenario("linux-vm0-concurrency", "vm2boot0", (69,))
def scenario_linux_vm0(ctx):
    """[69] VM0 advances while VM2 is in its Linux boot/running world."""
    s = ctx.s
    _attach_setup(ctx, 0, "\r\nrtos> ")

    def check_69():
        p = ctx.mark()
        end = time.time() + 5
        next_hb = time.time() + 0.5
        while time.time() < end:
            if time.time() >= next_hb:
                s.send("hb\n")
                next_hb += 2.0
            try:
                s.recv(0.5)
            except socket.timeout:
                pass
        w = s.buf[p:]
        ticks = [int(x) for x in re.findall(rb"rtos: alive tick=(\d+)", w)]
        if len(ticks) < 2:
            raise AssertionError(
                "hb replies < 2 in the dual-guest window: %r" % w[-300:])
        if ticks != sorted(ticks) or len(set(ticks)) != len(ticks):
            raise AssertionError("hb tick values not strictly increasing: %r"
                                 % ticks)

    ctx.check(69, NAMES[69], check_69)