#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Julian Chu
"""eshine-os week-1 true-green gate.

Boots eshine.elf on QEMU virt (direct EL2), waits for the banner and
the shell prompt, sends `echo ESHINEOK<pid>`, and asserts the token
appears at least TWICE: once as typed-echo by the guest shell, once as
actual command output. Static markers do not count; this mirrors the
login-dance + token-echo standard used by the previous eshine-os
reference repo (baseline alignment, spec section 5).
"""
import os
import socket
import subprocess
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ELF = os.path.join(REPO, "eshine.elf")
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
    if os.path.exists(SOCK):
        os.unlink(SOCK)

    qemu = subprocess.Popen(
        [
            "qemu-system-aarch64",
            "-M", "virt,virtualization=on,gic-version=3",
            "-cpu", "cortex-a57", "-m", "512",
            "-display", "none", "-monitor", "none",
            "-kernel", ELF,
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
            print("  [1/3] banner ok (" + VER + ")")
            buf = read_until(s, buf, b"eshine> ", time.time() + 10)
            print("  [2/3] shell prompt ok")
            prompt_pos = len(buf)          # 只在提示符之后的字节里找下一段
            token = "ESHINEOK%d" % os.getpid()
            s.sendall(("echo " + token + "\n").encode())
            buf = read_until(s, buf, b"eshine> ", time.time() + 10, start=prompt_pos)
            if buf[prompt_pos:].count(token.encode()) >= 2:
                print("  [3/3] token echoed back (typed-echo + command output)")
                print("RESULT PASS")
                return 0
            print(
                "FAIL: token seen %d time(s), expected >= 2 (typed-echo + output)\noutput:\n%s"
                % (buf.count(token.encode()), buf.decode(errors="replace"))
            )
            return 1
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
