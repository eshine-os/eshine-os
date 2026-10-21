#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Julian Chu
"""eshine-os true-green gate (week-1 core + first-guest C 相).

Boots eshine.elf on QEMU virt (direct EL2), waits for the banner and
the shell prompt, sends `echo ESHINEOK<pid>`, and asserts the token
appears at least TWICE: once as typed-echo by the guest shell, once as
actual command output. Static markers do not count; this mirrors the
login-dance + token-echo standard used by the previous eshine-os
reference repo (baseline alignment, spec section 5).

C 相 (27.04 第一站) covers the full guest round-trip: `run_guest`
shows the HVC banner printed from EL1 through the non-identity
stage-2 map, then the guest's own shell takes the UART over the
stage-2 device page (no hypercall), must double-echo its own token,
and `exit` HVCs back to EL2 with VM_DEAD. A final host token
double-echo proves the host shell survived the round trip.
"""
import os
import socket
import subprocess
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ELF = os.path.join(REPO, "eshine.elf")
GUEST1 = os.path.join(REPO, "guest1.bin")
GUEST2 = os.path.join(REPO, "guest2.bin")
SOCK = "/tmp/eshine-el2-verify.sock"
# VERSION 文件 = 唯一事实源,判据随版本自动同步(docs/VERSIONING.md)
VER = open(os.path.join(REPO, "VERSION")).read().strip()


def read_until(s, buf, needle, deadline, sock_timeout=1.0, start=0):
    """Read until needle appears in buf[start:] (bytes after `start`)."""
    while needle not in buf[start:]:
        if time.time() > deadline:
            raise TimeoutError(
                "timeout waiting for %r; output so far:\n%s"
                % (needle, buf.decode(errors="replace"))
            )
        s.settimeout(sock_timeout)
        try:
            chunk = s.recv(4096)
        except socket.timeout:
            continue
        if not chunk:
            raise ConnectionError("guest closed the serial socket")
        buf += chunk
    return buf


def main():
    if not os.path.exists(ELF):
        print("FAIL: %s not built (run make first)" % ELF)
        return 1
    for img in (GUEST1, GUEST2):
        if not os.path.exists(img):
            print("FAIL: %s not built (run make first)" % img)
            return 1
    if os.path.exists(SOCK):
        os.unlink(SOCK)

    qemu = subprocess.Popen(
        [
            "qemu-system-aarch64",
            "-M", "virt,virtualization=on,gic-version=3",
            "-cpu", "cortex-a57", "-m", "512",
            "-display", "none", "-monitor", "none",
            "-kernel", ELF,
            "-device", "loader,file=%s,addr=0x50000000,force-raw=on" % GUEST1,
            "-device", "loader,file=%s,addr=0x51000000,force-raw=on" % GUEST2,
            "-chardev", "socket,id=ser0,path=%s,server=on,wait=on" % SOCK,
            "-serial", "chardev:ser0",
        ],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    try:
        # wait=on: QEMU holds the guest until we connect, so no boot
        # output is lost between spawn and connect.
        deadline_connect = time.time() + 30
        while not os.path.exists(SOCK):
            if time.time() > deadline_connect:
                print("FAIL: serial socket never appeared")
                return 1
            time.sleep(0.1)
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.connect(SOCK)

        buf = b""
        try:
            buf = read_until(s, buf, ("eshine-os " + VER).encode(), time.time() + 30)
            print("  [1/8] banner ok (" + VER + ")")
            buf = read_until(s, buf, b"eshine> ", time.time() + 10)
            print("  [2/8] shell prompt ok")
            prompt_pos = len(buf)          # 只在提示符之后的字节里找下一段
            token = "ESHINEOK%d" % os.getpid()
            s.sendall(("echo " + token + "\n").encode())
            buf = read_until(s, buf, b"eshine> ", time.time() + 10, start=prompt_pos)
            if buf[prompt_pos:].count(token.encode()) < 2:
                print(
                    "FAIL: token seen %d time(s), expected >= 2 (typed-echo + output)\noutput:\n%s"
                    % (buf.count(token.encode()), buf.decode(errors="replace"))
                )
                return 1
            print("  [3/8] host token double-echo ok")

            # ---- C 相:guest 往返 + guest 侧 UART 交互 ----
            round_pos = len(buf)
            s.sendall(b"run_guest\n")
            buf = read_until(s, buf, b"eshine-guest VM0 @EL1", time.time() + 15, start=round_pos)
            print("  [4/8] guest HVC banner from EL1 (stage-2 + hypercall)")
            buf = read_until(s, buf, b"eshine-guest VM0 UART @IPA 0x09000000", time.time() + 10, start=round_pos)
            print("  [5/8] guest direct-UART banner (stage-2 device page)")
            buf = read_until(s, buf, b"vm0> ", time.time() + 10, start=round_pos)
            guest_pos = len(buf)
            gtoken = "GOK%d" % os.getpid()
            s.sendall(("echo " + gtoken + "\n").encode())
            buf = read_until(s, buf, b"vm0> ", time.time() + 10, start=guest_pos)
            if buf[guest_pos:].count(gtoken.encode()) < 2:
                print(
                    "FAIL: guest token seen %d time(s), expected >= 2\noutput:\n%s"
                    % (buf[guest_pos:].count(gtoken.encode()), buf.decode(errors="replace"))
                )
                return 1
            print("  [6/8] guest token double-echo ok (guest shell alive)")
            s.sendall(b"exit\n")
            buf = read_until(s, buf, b"run_guest: VM_DEAD, back at EL2", time.time() + 10, start=round_pos)
            print("  [7/8] VM_DEAD: guest HVC-exit back to EL2")
            buf = read_until(s, buf, b"eshine> ", time.time() + 10, start=round_pos)

            # 宿主完整性:guest 往返后 shell 仍须双回显
            prompt_pos = len(buf)
            token2 = "ESHINEOK%d" % (os.getpid() + 1)
            s.sendall(("echo " + token2 + "\n").encode())
            buf = read_until(s, buf, b"eshine> ", time.time() + 10, start=prompt_pos)
            if buf[prompt_pos:].count(token2.encode()) < 2:
                print(
                    "FAIL: post-guest token seen %d time(s), expected >= 2 (host corrupted?)\noutput:\n%s"
                    % (buf[prompt_pos:].count(token2.encode()), buf.decode(errors="replace"))
                )
                return 1
            print("  [8/8] host shell intact after guest round-trip")
            print("RESULT PASS")
            return 0
        finally:
            s.close()
    finally:
        qemu.terminate()
        try:
            qemu.wait(timeout=5)
        except subprocess.TimeoutExpired:
            qemu.kill()


if __name__ == "__main__":
    sys.exit(main())
