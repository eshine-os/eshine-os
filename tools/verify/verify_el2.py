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
            print("  [1/14] banner ok (" + VER + ")")
            buf = read_until(s, buf, b"eshine> ", time.time() + 10)
            print("  [2/14] shell prompt ok")
            prompt_pos = len(buf)          # 只在提示符之后的字节里找下一段
            token = "ESHINEOK%d" % os.getpid()
            s.sendall(("echo " + token + "\n").encode())
            buf = read_until(s, buf, b"eshine> ", time.time() + 10, start=prompt_pos)
            if buf[prompt_pos:].count(token.encode()) < 2:
                print(
                    "FAIL: host token seen %d time(s), expected >= 2\noutput:\n%s"
                    % (buf[prompt_pos:].count(token.encode()), buf.decode(errors="replace"))
                )
                return 1
            print("  [3/14] host token double-echo ok")

            def vm_round(tag, gtok, create):
                """(可选 vmcreate) + vmrun + guest token + exit -> VM_DEAD
                每个 read_until 都以发送时刻的 buf 长度为下界,拒绝陈旧匹配。"""
                nonlocal buf

                p = len(buf)
                if create:
                    s.sendall(("vmcreate " + tag + "\n").encode())
                    buf = read_until(s, buf, ("vm" + tag + ": CREATED").encode(),
                                     time.time() + 10, start=p)
                p = len(buf)
                s.sendall(("vmrun " + tag + "\n").encode())
                buf = read_until(s, buf,
                                 ("eshine-guest VM" + tag + " @EL1").encode(),
                                 time.time() + 15, start=p)
                print("  guest VM" + tag + " banner (stage-2 + HVC)")
                buf = read_until(s, buf,
                                 ("eshine-guest VM" + tag + " UART").encode(),
                                 time.time() + 10, start=p)
                buf = read_until(s, buf, ("vm" + tag + "> ").encode(),
                                 time.time() + 10, start=p)
                p = len(buf)
                s.sendall(("echo " + gtok + "\n").encode())
                buf = read_until(s, buf, ("vm" + tag + "> ").encode(),
                                 time.time() + 10, start=p)
                if buf[p:].count(gtok.encode()) < 2:
                    print("FAIL: VM%s token seen %d time(s)\noutput:\n%s"
                          % (tag, buf[p:].count(gtok.encode()),
                             buf.decode(errors="replace")))
                    return False
                print("  guest VM" + tag + " token double-echo ok")
                p = len(buf)
                s.sendall(b"exit\n")
                buf = read_until(s, buf, ("vm" + tag + ": VM_DEAD").encode(),
                                 time.time() + 10, start=p)
                # VM_DEAD 之后的宿主 prompt 可能还在路上 — 立即吃掉,
                # 否则下一条命令的 read_until 会撞上这个陈旧 prompt
                buf = read_until(s, buf, b"eshine> ", time.time() + 10, start=p)
                return True

            pos = len(buf)
            s.sendall(b"vmls\n")
            buf = read_until(s, buf, b"[0] VM_INIT", time.time() + 10, start=pos)
            buf = read_until(s, buf, b"[1] VM_INIT", time.time() + 10, start=pos)
            print("  [4/14] vmls: empty table (INIT, INIT)")

            pos = len(buf)
            s.sendall(b"vmcreate 0\n")
            buf = read_until(s, buf, b"vm0: CREATED", time.time() + 10, start=pos)
            pos = len(buf)
            s.sendall(b"vmls\n")
            buf = read_until(s, buf, b"[0] VM_CREATED", time.time() + 10, start=pos)
            print("  [5/14] vmcreate 0 -> CREATED")

            if not vm_round("0", "GOK%d" % os.getpid(), create=False):
                return 1
            print("  [6-8/14] VM0 round-trip ok (create/run/token/VM_DEAD)")

            if not vm_round("1", "GOKR%d" % os.getpid(), create=True):
                return 1
            print("  [9-11/14] VM1 round-trip ok (same IPA, different PA)")

            pos = len(buf)
            s.sendall(b"vmls\n")
            buf = read_until(s, buf, b"[0] VM_DEAD", time.time() + 10, start=pos)
            buf = read_until(s, buf, b"[1] VM_DEAD", time.time() + 10, start=pos)
            buf = read_until(s, buf, b"eshine> ", time.time() + 10, start=pos)
            print("  [12/14] vmls: full lifecycle DEAD")

            prompt_pos = len(buf)
            token2 = "ESHINEOK%d" % (os.getpid() + 1)
            s.sendall(("echo " + token2 + "\n").encode())
            buf = read_until(s, buf, b"eshine> ", time.time() + 10, start=prompt_pos)
            if buf[prompt_pos:].count(token2.encode()) < 2:
                print(
                    "FAIL: post-VM host token seen %d time(s), expected >= 2 (host corrupted?)\noutput:\n%s"
                    % (buf[prompt_pos:].count(token2.encode()), buf.decode(errors="replace"))
                )
                return 1
            print("  [13/14] host shell intact after two VM round-trips")

            pos = len(buf)
            s.sendall(b"vmkill 1\n")
            buf = read_until(s, buf, b"kill denied", time.time() + 10, start=pos)
            print("  [14/14] vmkill deny path ok (default-deny)")
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
