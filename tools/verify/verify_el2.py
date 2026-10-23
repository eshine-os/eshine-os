#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Julian Chu
"""eshine-os true-green gate (week-1 core + stops 1-5).

Boots eshine.elf on QEMU virt (direct EL2), waits for the banner and
the shell prompt, sends `echo ESHINEOK<pid>`, and asserts the token
appears at least TWICE: once as typed-echo by the guest shell, once as
actual command output. Static markers do not count; this mirrors the
login-dance + token-echo standard used by the previous eshine-os
reference repo (baseline alignment, spec section 5).

第五站(27.04)起调度异步化:vmrun 不再阻塞,双 VM 在 shell 等输入的
间隙被泵轮转(1 tick 量子,tick 出口帧转写抢占)。第六站 UART 撤出
stage-2 映射:DR/FR 访问全 data abort 陷阱化,宿主 per-VM vuart 按
寄存器语义仿真(guest 驱动不改语义),HVC #0/#3 退役,shell 恒排空
RX 按 owner 路由,两源等待(TIMER 注 LR/RX 直通)。判据——A 相:tick
严格递增;B 相:sleep→slept 注入全链;调度:双 RUNNING+交错 token 双
回显+spin 3G 前后 preempts 差>=2(转写无损)+WAIT 公平+后台醒;
设备:后台 banner [vmN] 前缀(陷阱 TX)+RX 唤醒链(入环→直通→FR
命中)+selftest 100 MMIO 陷阱+隔离死(窗口外默认拒绝)+vmconsole
状态门(novmrun 拒绝,防终端锁死);vtimer(第七站):per-VM ctx 切换,
双 VM 并发睡,后武装先醒(多等待者醒序判据)。门禁 24 步,
连续 2 轮才真绿。
"""
import os
import re
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


def tick_pair(s, buf, label):
    """两次 tick 读取,返回 (before, after);严格递增判据共用。"""
    p = len(buf)
    s.sendall(b"tick\n")
    buf = read_until(s, buf, b"ticks: ", time.time() + 10, start=p)
    i = buf.index(b"ticks: ", p) + len(b"ticks: ")
    buf = read_until(s, buf, b"\n", time.time() + 10, start=i)
    m = re.search(rb"ticks: (\d+)", buf[p:])
    if not m:
        raise AssertionError("tick output not parseable: %r" % buf[p:])
    before = int(m.group(1))
    time.sleep(2.5)
    p = len(buf)
    s.sendall(b"tick\n")
    buf = read_until(s, buf, b"ticks: ", time.time() + 10, start=p)
    i = buf.index(b"ticks: ", p) + len(b"ticks: ")
    buf = read_until(s, buf, b"\n", time.time() + 10, start=i)
    m = re.search(rb"ticks: (\d+)", buf[p:])
    if not m:
        raise AssertionError("tick output not parseable: %r" % buf[p:])
    after = int(m.group(1))
    if after <= before:
        raise AssertionError("%s: ticks not growing (%d -> %d)" % (label, before, after))
    return buf, before, after


def token_echo(s, buf, tok, deadline=10):
    """发送 echo <tok>,循环等 token 出现两次(回显+输出);返回 buf。
    注意不能用 read_until(\n) 两连:第一个 \n 在旧数据里,第二次匹配
    会立即返回而不收新输出(实测踩过)。"""
    p = len(buf)
    s.sendall(("echo " + tok + "\n").encode())
    end = time.time() + deadline
    while buf[p:].count(tok.encode()) < 2:
        if time.time() > end:
            raise AssertionError("token %r seen %d time(s)\nraw tail: %r"
                                 % (tok, buf[p:].count(tok.encode()),
                                    buf[p:][-160:]))
        s.settimeout(1.0)
        try:
            chunk = s.recv(4096)
        except socket.timeout:
            continue
        if not chunk:
            raise ConnectionError("serial closed while waiting for token")
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
            # ---- 宿主三连 ----
            buf = read_until(s, buf, ("eshine-os " + VER).encode(), time.time() + 30)
            print("  [1/27] banner ok (" + VER + ")")
            buf = read_until(s, buf, b"eshine> ", time.time() + 10)
            print("  [2/27] shell prompt ok")
            prompt_pos = len(buf)
            token = "ESHINEOK%d" % os.getpid()
            buf = token_echo(s, buf, token)
            print("  [3/27] host token double-echo ok")

            # ---- A 相:真中断判据 ----
            buf, b4, a4 = tick_pair(s, buf, "host tick")
            print("  [4/27] tick grows: %d -> %d (CNTHP->GICv3->EL2 IRQ)" % (b4, a4))

            pos = len(buf)
            s.sendall(b"vmls\n")
            buf = read_until(s, buf, b"[0] VM_INIT", time.time() + 10, start=pos)
            buf = read_until(s, buf, b"[1] VM_INIT", time.time() + 10, start=pos)
            print("  [5/27] vmls: empty table")

            pos = len(buf)
            s.sendall(b"vmcreate 0\n")
            buf = read_until(s, buf, b"vm0: CREATED", time.time() + 10, start=pos)
            print("  [6/27] vmcreate 0 -> CREATED")

            # ---- vmconsole 状态门:非 RUNNING 不可前台化(默认拒绝)。
            # owner 指给不消费输入的 VM = 终端永久锁死(输入全入环无人
            # 读,vmconsole host 自救同样被路由)——实锤修复的回归判据。 ----
            pos = len(buf)
            s.sendall(b"vmconsole 0\n")
            buf = read_until(s, buf, b"denied: not running", time.time() + 10, start=pos)
            print("  [7/27] vmconsole state gate: denied before vmrun")

            # ---- VM0:异步启动(后台跑动,banner 经 vuart 陷阱整行前缀冲) ----
            pos = len(buf)
            s.sendall(b"vmrun 0\n")
            buf = read_until(s, buf, b"eshine> ", time.time() + 10, start=pos)
            buf = read_until(s, buf, b"[vm0] eshine-guest VM0 @EL1", time.time() + 20,
                             start=pos)   # 后台 banner:[vmN] 前缀判据(第六站)
            print("  [8/27] vmrun 0 async -> background banner via vuart ([vm0] prefix)")

            # ---- RX 唤醒链:前台化,空行入环 -> 直通唤醒 -> FR 命中 ----
            pos = len(buf)
            s.sendall(b"vmconsole 0\n")
            buf = read_until(s, buf, b"console -> vm", time.time() + 10, start=pos)
            p2 = len(buf)
            s.sendall(b"\n")   # guest 在 WAIT-rx:入环即唤醒,重打 prompt
            buf = read_until(s, buf, b"vm0> ", time.time() + 20, start=p2)
            print("  [9/27] RX wake chain: push -> runnable -> FR hit -> prompt")

            p = len(buf)
            s.sendall(b"selftest\n")
            buf = read_until(s, buf, b"selftest OK", time.time() + 20, start=p)
            buf = read_until(s, buf, b"vm0> ", time.time() + 10, start=p)
            print("  [10/27] VM0 selftest OK (x19/x30 live across 100 MMIO traps)")

            time.sleep(2.5)   # 前台交互窗口(getc 陷阱化后无 HVC 风暴;纯计算
                              # 负载的抢占活性由 [16] spin 前后差断言)
            pos = len(buf)
            s.sendall(b"host\n")           # 前台 guest 内:HVC #4 交还 RX
            buf = read_until(s, buf, b"console -> host", time.time() + 10, start=pos)
            pos = len(buf)
            s.sendall(b"vmstat\n")
            buf = read_until(s, buf, b"preempts: ", time.time() + 15, start=pos)
            i = buf.index(b"preempts: ", pos) + len(b"preempts: ")
            buf = read_until(s, buf, b"\n", time.time() + 10, start=i)
            m = re.search(rb"preempts: (\d+)", buf[pos:])
            if not m:
                i2 = buf.rfind(b"preempts: ")
                raise AssertionError("vmstat unparseable; line=%r; tail=%r"
                                     % (buf[i2:i2+60] if i2 >= 0 else b"?", buf[-120:]))
            print("  [11/27] HVC#4 handback + vmstat readable (preempts=%s so far)"
                  % m.group(1).decode())

            # ---- VM1:第二 vCPU 上场,双活 ----
            pos = len(buf)
            s.sendall(b"vmcreate 1\n")
            buf = read_until(s, buf, b"vm1: CREATED", time.time() + 10, start=pos)
            s.sendall(b"vmrun 1\n")
            buf = read_until(s, buf, b"eshine> ", time.time() + 10, start=pos)
            buf = read_until(s, buf, b"[vm1] eshine-guest VM1 @EL1", time.time() + 20,
                             start=pos)   # 后台 banner 前缀判据(vm1 同律)
            print("  [12/27] vmrun 1 async -> background banner via vuart ([vm1] prefix)")
            pos = len(buf)
            s.sendall(b"vmconsole 1\n")
            buf = read_until(s, buf, b"console -> vm", time.time() + 10, start=pos)
            p2 = len(buf)
            s.sendall(b"\n")   # 同 [8]:入环唤醒,重打 prompt
            buf = read_until(s, buf, b"vm1> ", time.time() + 20, start=p2)
            pos2 = len(buf)
            s.sendall(b"host\n")           # 前台交还 RX
            buf = read_until(s, buf, b"console -> host", time.time() + 10, start=pos2)
            pos2 = len(buf)
            s.sendall(b"vmstat\n")
            buf = read_until(s, buf, b"[0] VM_RUNNING", time.time() + 10, start=pos2)
            buf = read_until(s, buf, b"[1] VM_RUNNING", time.time() + 10, start=pos2)
            buf = read_until(s, buf, b"preempts: ", time.time() + 10, start=pos2)
            i2 = buf.index(b"preempts: ", pos2) + len(b"preempts: ")
            buf = read_until(s, buf, b"\n", time.time() + 10, start=i2)
            m = re.search(rb"preempts: (\d+)", buf[pos2:])
            if not m:
                raise AssertionError("vmstat preempts line missing: %r" % buf[pos2:][-120:])
            # 后台双 idle guest 全 WFI 时无活负载即无抢占——preempts 数值
            # 只需可读;活负载下的轮转抢占由 [16] spin 判据断言(前后差 >=2)。
            print("  [13/27] both RUNNING (preempts=%s so far)" % m.group(1).decode())

            # ---- 双 VM 交错活(各自 token,HVC 路由) ----
            pos = len(buf)
            s.sendall(b"vmconsole 0\n")
            buf = read_until(s, buf, b"console -> vm", time.time() + 10, start=pos)
            p2 = len(buf)
            s.sendall(b"\n")   # 空行:触发 guest 重打 prompt(唤醒不重打)
            buf = read_until(s, buf, b"vm0> ", time.time() + 10, start=p2)
            buf = token_echo(s, buf, "GOKA%d" % os.getpid())
            pos2 = len(buf)
            s.sendall(b"host\n")           # 前台交还 RX
            buf = read_until(s, buf, b"console -> host", time.time() + 10, start=pos2)
            print("  [14/27] vm0 echo double-echo ok (foreground)")

            pos = len(buf)
            s.sendall(b"vmconsole 1\n")
            buf = read_until(s, buf, b"console -> vm", time.time() + 10, start=pos)
            p2 = len(buf)
            s.sendall(b"\n")   # 空行:触发 guest 重打 prompt(唤醒不重打)
            buf = read_until(s, buf, b"vm1> ", time.time() + 10, start=p2)
            buf = token_echo(s, buf, "GOKB%d" % os.getpid())
            pos2 = len(buf)
            s.sendall(b"host\n")           # 前台交还 RX
            buf = read_until(s, buf, b"console -> host", time.time() + 10, start=pos2)
            print("  [15/27] vm1 echo double-echo ok (both alive, interleaved)")

            # ---- 抢占转写正确性:前台 spin 3G 迭代(TCG 实测 ~0.4G/s,约 8s),
            # 跨多次量子切换;preempts 前后差 >=2 即证明 spin 期间被抢占且无损续跑 ----
            pos = len(buf)
            s.sendall(b"vmstat\n")
            buf = read_until(s, buf, b"preempts: ", time.time() + 10, start=pos)
            i = buf.index(b"preempts: ", pos) + len(b"preempts: ")
            buf = read_until(s, buf, b"\n", time.time() + 10, start=i)
            m = re.search(rb"preempts: (\d+)", buf[pos:])
            if not m:
                raise AssertionError("pre-spin vmstat unparseable: %r" % buf[pos:][-120:])
            pe0 = int(m.group(1))
            pos = len(buf)
            s.sendall(b"vmconsole 0\n")
            buf = read_until(s, buf, b"console -> vm", time.time() + 10, start=pos)
            p2 = len(buf)
            s.sendall(b"\n")   # 空行:触发 guest 重打 prompt(唤醒不重打)
            buf = read_until(s, buf, b"vm0> ", time.time() + 10, start=p2)
            p = len(buf)
            s.sendall(b"spin\n")
            buf = read_until(s, buf, b"spinning...", time.time() + 10, start=p)
            buf = read_until(s, buf, b"spun 3G iters (preempted ok)", time.time() + 60, start=p)
            pos2 = len(buf)
            s.sendall(b"host\n")           # 前台交还 RX
            buf = read_until(s, buf, b"console -> host", time.time() + 10, start=pos2)
            pos2 = len(buf)
            s.sendall(b"vmstat\n")
            buf = read_until(s, buf, b"preempts: ", time.time() + 10, start=pos2)
            i2 = buf.index(b"preempts: ", pos2) + len(b"preempts: ")
            buf = read_until(s, buf, b"\n", time.time() + 10, start=i2)
            m = re.search(rb"preempts: (\d+)", buf[pos2:])
            if not m:
                raise AssertionError("post-spin vmstat unparseable: %r" % buf[pos2:][-120:])
            pe1 = int(m.group(1))
            if pe1 - pe0 < 2:
                raise AssertionError("spin crossed %d quantum(s) (pe0=%d pe1=%d); "
                                     "frame migration unproven" % (pe1 - pe0, pe0, pe1))
            print("  [16/27] spin 3G survived %d quantum preemptions (frame migration intact)"
                  % (pe1 - pe0))

            # ---- WAIT 公平:vm0 睡,vm1 继续活;vm0 后台醒 ----
            pos = len(buf)
            s.sendall(b"vmconsole 0\n")
            buf = read_until(s, buf, b"console -> vm", time.time() + 10, start=pos)
            p2 = len(buf)
            s.sendall(b"\n")   # 空行:触发 guest 重打 prompt(唤醒不重打)
            buf = read_until(s, buf, b"vm0> ", time.time() + 10, start=p2)
            p = len(buf)
            s.sendall(b"sleep\n")
            buf = read_until(s, buf, b"sleeping...", time.time() + 10, start=p)
            time.sleep(0.5)   # WAIT 陷阱+owner 自动归还的间隙;FIFO 兜底不丢
            buf = token_echo(s, buf, "FAIR%d" % os.getpid())
            print("  [17/27] vm0 WAIT -> host alive, vm1 running (fairness)")

            buf = read_until(s, buf, b"slept (vtimer irq)", time.time() + 20)
            print("  [18/27] vm0 woke in background rotation (WOKE->pump inject)")

            # ---- 多等待者(第七站):双 VM 并发睡,per-VM ctx 各自到点醒 ----
            # vm0 先武装 4s,vm1 后武装 2s:后武装者先醒——单等待者时代
            # "武者互相覆盖,先睡者永不醒"的回归判据;醒序按流内位置断言。
            pos = len(buf)
            s.sendall(b"vmconsole 0\n")
            buf = read_until(s, buf, b"console -> vm", time.time() + 10, start=pos)
            p2 = len(buf)
            s.sendall(b"\n")
            buf = read_until(s, buf, b"vm0> ", time.time() + 10, start=p2)
            p = len(buf)
            s.sendall(b"sleep 4\n")
            buf = read_until(s, buf, b"sleeping...", time.time() + 10, start=p)
            pos = len(buf)
            s.sendall(b"vmconsole 1\n")
            buf = read_until(s, buf, b"console -> vm", time.time() + 10, start=pos)
            p2 = len(buf)
            s.sendall(b"\n")
            buf = read_until(s, buf, b"vm1> ", time.time() + 10, start=p2)
            p = len(buf)
            s.sendall(b"sleep 2\n")
            buf = read_until(s, buf, b"sleeping...", time.time() + 10, start=p)
            # 双 TIMER 等待:owner 按原因归还,宿主面可用;每拍扫描判到期
            buf = read_until(s, buf, b"[vm1] slept (vtimer irq)", time.time() + 15, start=p)
            p_ord = buf.index(b"[vm1] slept (vtimer irq)") + 1
            buf = read_until(s, buf, b"[vm0] slept (vtimer irq)", time.time() + 15,
                             start=p_ord)   # 位置在后:醒序=后武装者先醒
            print("  [19/27] dual sleep: per-VM vtimer ctx, later-armed wakes first")

            pos = len(buf)
            s.sendall(b"vmconsole 0\n")
            buf = read_until(s, buf, b"console -> vm", time.time() + 10, start=pos)
            p2 = len(buf)
            s.sendall(b"\n")
            buf = read_until(s, buf, b"vm0> ", time.time() + 10, start=p2)
            buf = token_echo(s, buf, "DUAL%d" % os.getpid())
            pos2 = len(buf)
            s.sendall(b"host\n")
            buf = read_until(s, buf, b"console -> host", time.time() + 10, start=pos2)
            print("  [20/27] both shells alive after dual sleep (vm0 echo ok)")

            # ---- vm0 退出;vm1 fault 隔离 ----
            pos = len(buf)
            s.sendall(b"vmconsole 0\n")
            buf = read_until(s, buf, b"console -> vm", time.time() + 10, start=pos)
            p2 = len(buf)
            s.sendall(b"\n")   # 空行触发重打 prompt(唤醒不重打)
            buf = read_until(s, buf, b"vm0> ", time.time() + 10, start=p2)
            p = len(buf)
            s.sendall(b"exit\n")
            buf = read_until(s, buf, b"vm0: VM_DEAD", time.time() + 15, start=p)
            print("  [21/27] vm0 exit -> VM_DEAD (RX auto-reclaimed)")

            pos = len(buf)
            s.sendall(b"vmconsole 1\n")
            buf = read_until(s, buf, b"console -> vm", time.time() + 10, start=pos)
            p2 = len(buf)
            s.sendall(b"\n")   # 空行触发重打 prompt(唤醒不重打)
            buf = read_until(s, buf, b"vm1> ", time.time() + 10, start=p2)
            p = len(buf)
            s.sendall(b"fault\n")
            buf = read_until(s, buf, b"fault: ESR=0x", time.time() + 10, start=p)
            buf = read_until(s, buf, b"vm1: faulted -> VM_DEAD", time.time() + 10, start=p)
            print("  [22/27] vm1 fault isolated: VM_DEAD, host alive")

            pos = len(buf)
            s.sendall(b"vmls\n")
            buf = read_until(s, buf, b"[0] VM_DEAD", time.time() + 10, start=pos)
            buf = read_until(s, buf, b"[1] VM_DEAD", time.time() + 10, start=pos)
            buf = read_until(s, buf, b"eshine> ", time.time() + 10, start=pos)
            print("  [23/27] vmls: both DEAD")

            prompt_pos = len(buf)
            token2 = "ESHINEOK%d" % (os.getpid() + 1)
            buf = token_echo(s, buf, token2)
            print("  [24/27] host shell intact after dual-VM lifecycle")

            # ---- guest 退场后 tick 仍增长(HCR/DAIF 恢复) ----
            buf, b20, a20 = tick_pair(s, buf, "post-guest tick")
            print("  [25/27] post-guest tick grows: %d -> %d (HCR_EL2_HOST/DAIF restore)"
                  % (b20, a20))

            pos = len(buf)
            s.sendall(b"vmkill 1\n")
            buf = read_until(s, buf, b"kill denied", time.time() + 10, start=pos)
            print("  [26/27] vmkill deny path ok (default-deny)")

            pos = len(buf)
            s.sendall(b"vmstat\n")
            buf = read_until(s, buf, b"[1] VM_DEAD", time.time() + 10, start=pos)
            print("  [27/27] vmstat still sane after teardown")
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
