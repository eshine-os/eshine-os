#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Julian Chu
"""smoke.py — 站级冒烟驱动(批验收纪律 2026-11-02:门禁写而不跑,
冒烟电池自证当场抓雷)。

与 verify_el2.py 同构的 QEMU 启动(同一构建产物,-smp 4 全拓扑),
但只跑一个场景(场景文件 = JSON 步骤序列),不跑全量门禁:

    [{"expect": "needle"},                          # 等输出(不发)
     {"send": "cmd", "expect": "needle",             # 发命令等判据
      "fail_if": "needle", "timeout": 30}]           # fail_if=步内禁现

fail_if 只查本步起的新输出(pos 纪律)——早前缓冲里的同串不算。
socket/日志 = 会话独占路径(pid 后缀,防并行会话互染)。

用法: python3 tools/verify/smoke.py scene.json [scene2.json ...]
"""
import json
import os
import socket
import subprocess
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ELF = os.path.join(REPO, "eshine.elf")
RTOS_BIN = os.path.join(REPO, "rtos.bin")
GUEST2 = os.path.join(REPO, "guest2.bin")
SOCK = "/tmp/eshine-smoke-%d.sock" % os.getpid()
LOG = "/tmp/eshine-smoke-%d.log" % os.getpid()
LINUX_IMG = os.environ.get(
    "ESHINE_LINUX_IMAGE",
    os.path.expanduser("~/projects/eshine-linux/linux-6.6/arch/arm64/boot/Image"))
INITRAMFS = os.environ.get(
    "ESHINE_INITRAMFS",
    os.path.expanduser("~/projects/eshine-linux/initramfs.cpio"))


def send_line(s, data):
    """8 字节分块(QEMU pl011 RX FIFO 16 字节上限,站十四 token 丢尾
    实测;分块间让泵轮转消化)——与 verify_el2.py send_guest 同律。"""
    for i in range(0, len(data), 8):
        s.sendall(data[i:i + 8])
        time.sleep(0.05)


def read_until(s, buf, needle, deadline, sock_timeout=1.0, start=0):
    while needle not in buf[start:]:
        if time.time() > deadline:
            raise TimeoutError(
                "timeout waiting for %r; new output:\n%s"
                % (needle, buf[start:].decode(errors="replace")))
        s.settimeout(sock_timeout)
        try:
            chunk = s.recv(4096)
        except socket.timeout:
            continue
        if not chunk:
            raise ConnectionError("guest closed the serial socket")
        buf += chunk
    time.sleep(0.12)
    s.settimeout(0.05)
    drain_cap = time.time() + 0.25
    try:
        while time.time() < drain_cap:
            chunk = s.recv(4096)
            if not chunk:
                break
            buf += chunk
    except socket.timeout:
        pass
    return buf


def run_scene(name, steps):
    qemu = subprocess.Popen(
        [
            "qemu-system-aarch64",
            "-M", "virt,virtualization=on,gic-version=3",
            "-cpu", "cortex-a57", "-m", "512",
            "-smp", "4",
            "-display", "none", "-monitor", "none",
            "-kernel", ELF,
            "-device", "loader,file=%s,addr=0x50000000,force-raw=on" % RTOS_BIN,
            "-device", "loader,file=%s,addr=0x51000000,force-raw=on" % GUEST2,
            "-device", "loader,file=%s,addr=0x58080000,force-raw=on" % LINUX_IMG,
            "-device", "loader,file=%s,addr=0x5C000000,force-raw=on" % INITRAMFS,
            "-chardev", "socket,id=ser0,path=%s,server=on,wait=on" % SOCK,
            "-serial", "chardev:ser0",
        ],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    try:
        for _ in range(100):
            if os.path.exists(SOCK):
                break
            time.sleep(0.05)
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.connect(SOCK)
        log = open(LOG, "ab")
        buf = b""
        t0 = time.time()

        def recv_all():
            nonlocal buf
            s.settimeout(0.2)
            try:
                while True:
                    chunk = s.recv(4096)
                    if not chunk:
                        break
                    buf += chunk
                    log.write(chunk)
            except socket.timeout:
                pass

        # boot:banner 即交接(场景从 banner 后接管——boot 行/提示符
        # 都是场景步骤,pos 纪律保位置正确)
        banner = ("eshine-os " + open(os.path.join(REPO, "VERSION")).read().strip()).encode()
        while time.time() < t0 + 60 and banner not in buf:
            s.settimeout(0.3)
            try:
                d = s.recv(4096)
                if d:
                    buf += d
            except socket.timeout:
                pass
        if banner not in buf:
            raise TimeoutError("boot: banner not seen in 60s")
        print("  boot ok (%.1fs) — %s" % (time.time() - t0, name))
        passed = 0
        epoch = 0                    # 命令纪元:最近一次 send 时的 buf 位
        try:
            for i, st in enumerate(steps, 1):
                pos = len(buf)
                if "send" in st:
                    epoch = pos       # 新纪元=本命令起(排空窗内早到的
                                     # 输出归属本纪元,等待型步骤不丢)
                    send_line(s, st["send"].encode() + b"\n")
                search = pos if "send" in st else epoch
                deadline = time.time() + st.get("timeout", 60)
                buf = read_until(s, buf, st["expect"].encode(), deadline, start=search)
                if "fail_if" in st:
                    if st["fail_if"].encode() in buf[search:]:
                        raise AssertionError(
                            "step %d: fail_if needle present: %r"
                            % (i, st["fail_if"]))
                recv_all()
                print("  [%d/%d] %s -> %s%s"
                      % (i, len(steps), st.get("send", "(wait)"),
                         st["expect"],
                         " [absent: %s]" % st["fail_if"] if "fail_if" in st else ""))
                passed += 1
        finally:
            log.close()
            s.close()
        return passed, len(steps)
    finally:
        qemu.terminate()
        try:
            qemu.wait(5)
        except Exception:
            qemu.kill()
        if os.path.exists(SOCK):
            os.unlink(SOCK)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    ok = True
    for scene in sys.argv[1:]:
        with open(scene) as f:
            steps = json.load(f)
        name = os.path.basename(scene)
        print("smoke: %s (%d steps)" % (name, len(steps)))
        try:
            p, n = run_scene(name, steps)
            print("smoke: %s PASS %d/%d (log %s)" % (name, p, n, LOG))
        except Exception as e:
            print("smoke: %s FAIL: %s (log %s)" % (name, e, LOG))
            ok = False
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
