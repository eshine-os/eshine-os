# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Julian Chu
"""Boot/arrival recipes shared by fixtures and scenarios.

These are infrastructure steps, not gate results: a failure here means the
fixture could not be established and the affected gates are BLOCKED.
"""

from __future__ import annotations

import os
import time

from .artifacts import Env
from .session import FloodError


def qemu_args(env: Env, sock: str, *,
              with_linux: bool = True,
              gdb_port: int | None = None) -> list[str]:
    args = [
        env.qemu,
        "-M", "virt,virtualization=on,gic-version=3",
        "-cpu", "cortex-a57", "-m", "512", "-smp", "4",
        "-display", "none", "-monitor", "none",
        "-kernel", env.elf,
        "-device", "loader,file=%s,addr=0x50000000,force-raw=on"
        % env.rtos_bin,
        "-device", "loader,file=%s,addr=0x51000000,force-raw=on"
        % env.guest2,
    ]
    if with_linux:
        if os.path.exists(env.linux_img):
            args += ["-device",
                     "loader,file=%s,addr=0x58080000,force-raw=on"
                     % env.linux_img]
        if os.path.exists(env.initramfs):
            args += ["-device",
                     "loader,file=%s,addr=0x5C000000,force-raw=on"
                     % env.initramfs]
    args += ["-chardev", "socket,id=ser0,path=%s,server=on,wait=on" % sock,
             "-serial", "chardev:ser0"]
    if gdb_port is not None:
        args += ["-gdb", "tcp::%d" % gdb_port]
    return args


def boot_host(s, env: Env, *, timeout_s: float = 30.0) -> None:
    """Session start already consumed the banner; wait for the host prompt."""
    if env.banner not in s.buf:
        s.expect(env.banner, timeout_s)
    s.expect("eshine> ", timeout_s)


def wait_quiet(s, *, quiet_s: float = 0.25, timeout_s: float = 5.0,
               anchor: str | bytes = "svc: governed") -> None:
    """Wait until the VM0 boot output quiesces (or the anchor appears).

    Station 69: replaces the fixed 2.5 s pre-attach sleep.  The service
    storm after `init: inittab processed` ends with `svc: governed`; on
    a quiet boot nothing more arrives at all.  Either way we attach as
    soon as the console stops printing, instead of always paying 2.5 s
    per VM0 boot (100+ gates).
    """
    import socket

    anchor_b = anchor.encode() if isinstance(anchor, str) else anchor
    deadline = time.time() + timeout_s
    last_len = len(s.buf)
    quiet_since = time.time()
    while time.time() < deadline:
        if anchor_b and anchor_b in s.buf:
            return
        try:
            s.recv(timeout=0.1)
        except socket.timeout:
            if time.time() - quiet_since > quiet_s:
                return
            continue
        except (ConnectionError, OSError):
            return
        if len(s.buf) != last_len:
            last_len = len(s.buf)
            quiet_since = time.time()


def vm0_ready(s, env: Env, *, attach: bool = True,
              boot_timeout_s: float = 30.0) -> None:
    """Create and run VM0, wait for init/services, optionally attach."""
    p = s.mark()
    s.send("vmcreate 0\n")
    s.expect("vm0: CREATED", 30.0, start=p)
    p = s.mark()
    s.send("vmrun 0\n")
    s.expect("[vm0] eshine-rtos", boot_timeout_s, start=p)
    s.expect("[vm0] rtos: up (eshine-rtos", boot_timeout_s, start=p)
    s.expect("srv: pm bound stdio", boot_timeout_s, start=p)
    s.expect("init: inittab processed", boot_timeout_s, start=p)
    if s.flooded(p):
        raise FloodError("all-space flood during VM0 boot: %r"
                         % s.buf[p:][-500:])
    if attach:
        wait_quiet(s)
        attach_vm(s, 0, "\r\nrtos> ", pre_sleep_s=0.0,
                  wake_retries=6, timeout_s=25.0)


def vm1_boot(s, env: Env, *, timeout_s: float = 60.0) -> None:
    """Create/run VM1 and wait for its banner; console stays on the host."""
    p = s.mark()
    s.send("vmcreate 1\n")
    s.expect("vm1: CREATED", 30.0, start=p)
    p = s.mark()
    s.send("vmrun 1\n")
    s.expect("eshine> ", 30.0, start=p)
    s.expect("[vm1] eshine-guest VM1", timeout_s, start=p)
    s.expect("[vm1] uart irq on", timeout_s, start=p)


def vm1_ready(s, env: Env, *, timeout_s: float = 60.0) -> None:
    vm1_boot(s, env, timeout_s=timeout_s)
    attach_vm(s, 1, "\nvm1> ", pre_sleep_s=0.0,
              wake_retries=6, timeout_s=25.0)


def vm2_ready(s, env: Env, *, timeout_s: float = 300.0) -> None:
    p = s.mark()
    s.send("vmcreate 2\n")
    s.expect("vm2: CREATED", 30.0, start=p)
    p = s.mark()
    s.send("vmrun 2\n")
    s.expect("Booting Linux on physical CPU", timeout_s, start=p)
    s.expect("smp: Brought up 1 node, 2 CPUs", timeout_s, start=p)
    s.expect("Freeing unused kernel memory", timeout_s, start=p)
    s.expect("Run /init as init process", timeout_s, start=p)
    attach_vm(s, 2, "~ # ", pre_sleep_s=0.0,
              wake_retries=8, timeout_s=90.0)


def attach_vm(s, vm_id: int, prompt: str | bytes, *,
              pre_sleep_s: float = 0.0, wake_retries: int = 8,
              timeout_s: float = 60.0) -> None:
    """Switch the console to a guest and wake its shell prompt.

    RX injection can lose a byte under load (station 23/24 family); the
    bounded newline retries keep a lost wake from failing the fixture.
    The anchor still only accepts a real prompt (CRLF + prompt), never a
    startup half-line.
    """
    p = s.mark()
    s.send("vmconsole %d\n" % vm_id)
    s.expect("console -> vm", timeout_s, start=p)
    if pre_sleep_s:
        time.sleep(pre_sleep_s)
    p = s.mark()
    last: Exception | None = None
    deadline = time.time() + timeout_s
    # Pump often: the RX wake loss (station 23/24 family) drops a byte,
    # it does not make the guest slow, so a 12s wait per newline spends
    # most of the attach budget on one lost byte.  Each pump re-sends the
    # wake and waits only ~1.5s before trying again; `wake_retries` sets
    # a bounded number of pumps and `timeout_s` bounds the whole attach.
    max_tries = max(1, wake_retries) * 5
    for _ in range(max_tries):
        if time.time() > deadline:
            break
        if s.flooded(p):
            raise FloodError("all-space flood at console attach: %r"
                             % s.buf[p:][-500:])
        s.send("\n")
        try:
            s.expect(prompt, min(1.5, max(0.2, deadline - time.time())),
                     start=p)
            return
        except TimeoutError as e:
            last = e
            if s.flooded(p):
                raise FloodError("all-space flood at console attach: %r"
                                 % s.buf[p:][-500:])
    if s.flooded(p):
        raise FloodError("all-space flood while waiting for %r: %r"
                         % (prompt, s.buf[p:][-500:]))
    raise TimeoutError(
        "wake prompt %r never appeared after %d retries; output so far:\n%s"
        % (prompt, max_tries,
           s.buf[p:][-4000:].decode(errors="replace"))) from last


def escape_pump(s, anchor: str | bytes = "console -> host", *,
                timeout_s: float = 25.0) -> None:
    """Send the 0x1d escape and re-send until the anchor appears.

    The escape byte travels the same RX injection path as the attach
    wake (station 23/24 lost-byte family); one send + long wait loses
    the whole scenario on a dropped byte.  Re-send every ~1.5s within
    the deadline instead.
    """
    p = s.mark()
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        s.send(b"\x1d")
        try:
            s.expect(anchor, min(1.5, max(0.2, deadline - time.time())),
                     start=p)
            return
        except TimeoutError:
            continue
    raise TimeoutError(
        "escape anchor %r never appeared; output so far:\n%s"
        % (anchor, s.buf[p:][-2000:].decode(errors="replace")))


def escape_to_host(s, *, timeout_s: float = 25.0) -> None:
    escape_pump(s, "console -> host", timeout_s=timeout_s)


def wake_host(s, *, timeout_s: float = 25.0) -> None:
    """Wake the host shell after a guest death (owner already NULL).

    A fault/off teardown returns the console to the host without
    printing a prompt (vm_stop sets sched_console_owner = NULL), and
    0x1d is only an escape while a guest still owns the console.  Pump
    newlines to run the host shell's line loop and print `eshine> `.
    """
    p = s.mark()
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        s.send("\n")
        try:
            s.expect("eshine> ", min(1.5, max(0.2, deadline - time.time())),
                     start=p)
            return
        except TimeoutError:
            continue
    raise TimeoutError(
        "host prompt never appeared after guest death; output so far:\n%s"
        % s.buf[p:][-2000:].decode(errors="replace"))


def hotkey(s, key: str, *, expect_vm: bool = True,
           timeout_s: float = 5.0) -> None:
    p = s.mark()
    s.send(b"\x1e" + key.encode())
    s.expect("console -> vm" if expect_vm else "console -> host",
             timeout_s, start=p)