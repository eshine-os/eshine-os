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
设备:后台 banner [vmN] 前缀(陷阱 TX)+RX 唤醒链(入环→直通→注入
取数)+uart irq on(RXIM 使能,vSPI 33 虚注)+selftest 100 MMIO 陷阱+
隔离死(窗口外默认拒绝)+vmconsole 状态门(novmrun 拒绝);vtimer
(第七站):per-VM ctx 切换,双 VM 并发睡,后武装先醒(醒序判据);
vblk(第九站):门铃+guest 内存命令块,
dwrite/dread 完成中断全链(vSPI 34),
销毁重建数据持久。门禁 55 步,
连续 2 轮才真绿。

第十六站(27.04):VM0 槽升格为 eshine-rtos(参考RTOS 公开设计为基准的
最小任务内核:32 级位图抢占+tick 100Hz+IRQ 驱动壳);guest1.bin 退役,
guest2.bin(裸机)留任 VM1 对照组;SIMD per-VM 帧入泵(双 FP 客并跑
前置)。新判据:rtos up/ps/spawn/fp 自证/sleep 心跳夹行/yield/双客
并发时序证明(心跳行夹在 Linux boot 行流之间)+ vm0 exit 迟退步;
旧 vm0 裸机系判据(dwrite/dread/spin/WAIT 公平/持久化)全数重锚
vm1 同强度;双睡判据演进为"RTOS 周期 tick 与裸机 one-shot 并存
醒序"(per-VM vtimer ctx 更强形态)。
"""
import os
import re
import socket
import subprocess
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ELF = os.path.join(REPO, "eshine.elf")
RTOS_BIN = os.path.join(REPO, "rtos.bin")   # 站十六:VM0=eshine-rtos
GUEST2 = os.path.join(REPO, "guest2.bin")
SOCK = "/tmp/eshine-el2-verify.sock"
# 第十四站:Linux guest 内核(槽 2)。可从源构建的工件,不入仓(GPL 隔离),
# 配方 = scripts/build-linux.sh;缺它则 Linux 判据无从验起,明确指引后 FAIL。
LINUX_IMG = os.environ.get(
    "ESHINE_LINUX_IMAGE",
    os.path.expanduser("~/projects/eshine-linux/linux-6.6/arch/arm64/boot/Image"))
# 第十五站:initramfs(busybox 用户空间)。外部工件同 Image(GPL 隔离),
# 配方 = scripts/build-initramfs.sh;定长 8MB 槽 @IPA 0x44000000 =
# 宿主 0x5C000000(stage-2 非恒等映射),与 dts linux,initrd-* 绑死。
INITRAMFS = os.environ.get(
    "ESHINE_INITRAMFS",
    os.path.expanduser("~/projects/eshine-linux/initramfs.cpio"))
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


def send_guest(s, data):
    """向 guest 发命令:8 字节分块(QEMU pl011 RX FIFO 16 字节上限,
    站十四 token 丢尾实测;分块间让泵轮转消化)。"""
    for i in range(0, len(data), 8):
        s.sendall(data[i:i + 8])
        time.sleep(0.05)


def token_echo(s, buf, tok, deadline=10):
    """发送 echo <tok>,循环等 token 出现两次(回显+输出);返回 buf。
    注意不能用 read_until(\n) 两连:第一个 \n 在旧数据里,第二次匹配
    会立即返回而不收新输出(实测踩过)。"""
    p = len(buf)
    # 站十四:命令分块发——QEMU pl011 RX FIFO 16 字节,guest 在跑时宿主
    # 排空慢,一次性 sendall 长命令会丢尾(Linux panic 后实测)。
    cmd = ("echo " + tok + "\n").encode()
    for i in range(0, len(cmd), 8):
        s.sendall(cmd[i:i + 8])
        time.sleep(0.03)
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
    for img in (RTOS_BIN, GUEST2):
        if not os.path.exists(img):
            print("FAIL: %s not built (run make first)" % img)
            return 1
    if not os.path.exists(LINUX_IMG):
        print("FAIL: Linux Image missing: %s" % LINUX_IMG)
        print("      run scripts/build-linux.sh to build it (station-14 gate "
              "requires the Linux guest)")
        return 1
    if not os.path.exists(INITRAMFS):
        print("FAIL: initramfs missing: %s" % INITRAMFS)
        print("      run scripts/build-initramfs.sh (station-15 gate requires "
              "the busybox userland)")
        return 1
    if os.path.getsize(INITRAMFS) != 8 * 1024 * 1024:
        print("FAIL: initramfs must be exactly 8MB (fixed-slot contract "
              "with dts linux,initrd-end)")
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
            "-device", "loader,file=%s,addr=0x50000000,force-raw=on" % RTOS_BIN,
            "-device", "loader,file=%s,addr=0x51000000,force-raw=on" % GUEST2,
            # 槽 2 Linux:Image@PA 0x58080000(基址+text_offset 0x80000)
            #            initramfs@PA 0x5C000000(IPA 0x44000000 经 stage-2)
            "-device", "loader,file=%s,addr=0x58080000,force-raw=on" % LINUX_IMG,
            "-device", "loader,file=%s,addr=0x5C000000,force-raw=on" % INITRAMFS,
            "-chardev", "socket,id=ser0,path=%s,server=on,wait=on" % SOCK,
            "-serial", "chardev:ser0",
        ] + (["-d", "int"] if os.environ.get("ESHINE_INTLOG") else []),
        stdout=subprocess.DEVNULL,
        stderr=open(os.environ.get("ESHINE_INTLOG", "/dev/null"), "w") if
            os.environ.get("ESHINE_INTLOG") else subprocess.DEVNULL,
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
            print("  [1/55] banner ok (" + VER + ")")
            buf = read_until(s, buf, b"eshine> ", time.time() + 10)
            print("  [2/55] shell prompt ok")
            prompt_pos = len(buf)
            token = "ESHINEOK%d" % os.getpid()
            buf = token_echo(s, buf, token)
            print("  [3/55] host token double-echo ok")

            # ---- A 相:真中断判据 ----
            buf, b4, a4 = tick_pair(s, buf, "host tick")
            print("  [4/55] tick grows: %d -> %d (CNTHP->GICv3->EL2 IRQ)" % (b4, a4))

            pos = len(buf)
            s.sendall(b"vmls\n")
            buf = read_until(s, buf, b"[0] VM_INIT", time.time() + 10, start=pos)
            buf = read_until(s, buf, b"[1] VM_INIT", time.time() + 10, start=pos)
            print("  [5/55] vmls: empty table")

            pos = len(buf)
            s.sendall(b"vmcreate 0\n")
            buf = read_until(s, buf, b"vm0: CREATED", time.time() + 10, start=pos)
            print("  [6/55] vmcreate 0 -> CREATED")

            # ---- vmconsole 状态门:非 RUNNING 不可前台化(默认拒绝)。
            # owner 指给不消费输入的 VM = 终端永久锁死(输入全入环无人
            # 读,vmconsole host 自救同样被路由)——实锤修复的回归判据。 ----
            pos = len(buf)
            s.sendall(b"vmconsole 0\n")
            buf = read_until(s, buf, b"denied: not running", time.time() + 10, start=pos)
            print("  [7/55] vmconsole state gate: denied before vmrun")

            # ---- VM0:异步启动(后台跑动,banner 经 vuart 陷阱整行前缀冲) ----
            # 站十六:VM0 槽 = eshine-rtos(任务内核客);banner 即 RTOS 身份
            pos = len(buf)
            s.sendall(b"vmrun 0\n")
            buf = read_until(s, buf, b"eshine> ", time.time() + 10, start=pos)
            buf = read_until(s, buf, ("[vm0] eshine-rtos " + VER).encode(),
                             time.time() + 20, start=pos)   # 后台 banner:[vmN] 前缀判据(第六站)
            print("  [8/55] vmrun 0 async -> rtos banner via vuart ([vm0] prefix)")

            # ---- DTB 自查(第十一站):三设备身份从注入树查出(非硬编码) ----
            buf = read_until(s, buf, b"[vm0] fdt ok: cpu arm,cortex-a57",
                             time.time() + 10, start=pos)
            print("  [9/55] fdt self-check: cpu node (arm,cortex-a57)")
            buf = read_until(s, buf, b"[vm0] fdt ok: uart arm,pl011",
                             time.time() + 10, start=pos)
            print("  [10/55] fdt self-check: uart node (arm,pl011)")
            buf = read_until(s, buf, b"[vm0] fdt ok: blk virtio,mmio",
                             time.time() + 10, start=pos)
            print("  [11/55] fdt self-check: blk node (virtio,mmio)")

            # ---- PL011 完整化(第十三站):AMBA 身份证+开机(CR 真语义) ----
            buf = read_until(s, buf, b"[vm0] pl011 on: amba 00141011",
                             time.time() + 10, start=pos)
            print("  [12/55] pl011 amba id 0x00141011 (PERIPHID/PCELLID probe)")

            # ---- vGIC 真启动式初始化(第十二站):distributor 成为注入权威 ----
            buf = read_until(s, buf, b"[vm0] gicd v3 ok", time.time() + 10,
                             start=pos)
            print("  [13/55] gicd v3 probe via emulated GICD (PIDR2 == 0x30)")
            buf = read_until(s, buf, b"[vm0] gic on: 27 33 35", time.time() + 10,
                             start=pos)
            print("  [14/55] gic enabled via GICD/GICR (ppi27 + spi33/35 readback)")

            # ---- UART IRQ 模式(第八站):guest 使能 RXIM 后自报 ----
            buf = read_until(s, buf, b"[vm0] uart irq on", time.time() + 10,
                             start=pos)
            print("  [15/55] guest enabled RXIM -> uart irq mode (vSPI 33 path)")

            # ---- virtio 就绪(第十站):probe 三验+队列+DRIVER_OK 自报 ----
            buf = read_until(s, buf, b"[vm0] virtio blk on", time.time() + 10,
                             start=pos)
            print("  [16/55] virtio probe + DRIVER_OK -> virtio blk on (vSPI 35 path)")

            # ---- 站十六:RTOS 世界起立(位图调度器+tick+任务) ----
            buf = read_until(s, buf, b"[vm0] rtos: up (eshine-rtos", time.time() + 10,
                             start=pos)
            print("  [17/55] rtos up: 32-prio bitmap scheduler, tick 100Hz")

            # ---- RX 唤醒链:前台化,空行入环 -> 直通唤醒 -> FR 命中 ----
            pos = len(buf)
            s.sendall(b"vmconsole 0\n")
            buf = read_until(s, buf, b"console -> vm", time.time() + 10, start=pos)
            p2 = len(buf)
            s.sendall(b"\n")   # RTOS 壳任务 BLOCK 中:入环即唤醒,重打 prompt
            buf = read_until(s, buf, b"rtos> ", time.time() + 20, start=p2)
            print("  [18/55] RX wake chain: push -> irq -> shell task READY -> prompt")

            p = len(buf)
            s.sendall(b"selftest\n")
            buf = read_until(s, buf, b"selftest OK", time.time() + 20, start=p)
            buf = read_until(s, buf, b"rtos> ", time.time() + 10, start=p)
            print("  [19/55] VM0 (rtos) selftest OK (x19/x30 live across 100 MMIO traps)")

            # ---- 站十六:ps——任务表活体(shell/heartbeat/idle 三行在场) ----
            p = len(buf)
            s.sendall(b"ps\n")
            buf = read_until(s, buf, b"rtos> ", time.time() + 10, start=p)
            for row in (b"shell", b"heartbeat", b"idle"):
                if row not in buf[p:]:
                    raise AssertionError("ps missing row %r: %r" % (row, buf[p:][-200:]))
            print("  [20/55] ps: shell/heartbeat/idle rows (task table live)")

            # ---- 站十六:spawn——动态任务生-问候-亡全链 ----
            p = len(buf)
            send_guest(s, b"spawn gate16\n")
            buf = read_until(s, buf, b"gate16: hello from task", time.time() + 15, start=p)
            buf = read_until(s, buf, b"rtos> ", time.time() + 10, start=p)
            print("  [21/55] spawn gate16 -> born, greeted, exited to pool")

            # ---- 站十六:fp 自证——1e6 double 和跨任务/跨客切换精确 ----
            p = len(buf)
            s.sendall(b"fp\n")
            buf = read_until(s, buf, b"rtos> ", time.time() + 10, start=p)   # 壳立回(任务异步)
            buf = read_until(s, buf, b"fp OK: 500000500000", time.time() + 60, start=p)
            print("  [22/55] fp self-proof: 1e6 double sum exact (SIMD frame across VM/task switches)")

            # ---- 站十六:壳任务睡 3s,心跳任务不睡——窗口内必有 alive 行 ----
            p = len(buf)
            s.sendall(b"sleep 3\n")
            buf = read_until(s, buf, b"sleeping...", time.time() + 10, start=p)
            buf = read_until(s, buf, b"slept (rtos sleep)", time.time() + 20, start=p)
            p_end = buf.index(b"slept (rtos sleep)", p)
            if b"rtos: alive" not in buf[p:p_end]:
                raise AssertionError("no heartbeat during shell sleep: %r" % buf[p:p_end][-200:])
            print("  [23/55] shell sleep 3s: heartbeat interleaved (task-level concurrency)")

            # ---- 站十六:yield——HVC#2 让出-PAUSED-vmrun 续跑(任务语境存活) ----
            # 注:YIELD→PAUSED 时宿主自动收台(PAUSED 客无人读 RX;首跑实
            # 测:此刻发 0x1d 会当普通字符进宿主行编辑器)——无需逃生,宿主
            # 直接 vmrun 续跑;壳任务在 hvc_yield 返回点续印 resumed。
            p = len(buf)
            s.sendall(b"yield\n")
            buf = read_until(s, buf, b"yielding...", time.time() + 10, start=p)
            buf = read_until(s, buf, b"yielded -> PAUSED", time.time() + 10, start=p)
            pos = len(buf)
            s.sendall(b"vmrun 0\n")
            buf = read_until(s, buf, b"[vm0] resumed", time.time() + 15, start=pos)
            print("  [24/55] HVC#2 yield -> PAUSED -> vmrun resume -> '[vm0] resumed' (task ctx intact)")

            # ---- host 交还+vmstat(裸机 vm1 未上场,preempts 只需可读) ----
            pos = len(buf)
            s.sendall(b"vmconsole 0\n")
            buf = read_until(s, buf, b"console -> vm", time.time() + 10, start=pos)
            p2 = len(buf)
            s.sendall(b"\n")
            buf = read_until(s, buf, b"rtos> ", time.time() + 10, start=p2)
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
            print("  [25/55] HVC#4 handback + vmstat readable (preempts=%s so far)"
                  % m.group(1).decode())

            # ---- VM1:裸机对照组上场(站十六:guest2.bin 留任) ----
            pos = len(buf)
            s.sendall(b"vmcreate 1\n")
            buf = read_until(s, buf, b"vm1: CREATED", time.time() + 10, start=pos)
            s.sendall(b"vmrun 1\n")
            buf = read_until(s, buf, b"eshine> ", time.time() + 10, start=pos)
            buf = read_until(s, buf, b"[vm1] eshine-guest VM1 @EL1", time.time() + 20,
                             start=pos)   # 后台 banner 前缀判据(vm1 同律)
            print("  [26/55] vmrun 1 async -> bare-metal banner via vuart ([vm1] prefix)")
            pos = len(buf)
            s.sendall(b"vmconsole 1\n")
            buf = read_until(s, buf, b"console -> vm", time.time() + 10, start=pos)
            p2 = len(buf)
            s.sendall(b"\n")   # 同 [18]:入环唤醒,重打 prompt
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
            print("  [27/55] both RUNNING (preempts=%s so far)" % m.group(1).decode())

            # ---- 双客交错活:RTOS 壳 token(HVC 路由) ----
            pos = len(buf)
            s.sendall(b"vmconsole 0\n")
            buf = read_until(s, buf, b"console -> vm", time.time() + 10, start=pos)
            p2 = len(buf)
            s.sendall(b"\n")   # 空行:触发壳任务重打 prompt
            buf = read_until(s, buf, b"rtos> ", time.time() + 10, start=p2)
            buf = token_echo(s, buf, "GOKA%d" % os.getpid())
            pos2 = len(buf)
            s.sendall(b"host\n")           # 前台交还 RX
            buf = read_until(s, buf, b"console -> host", time.time() + 10, start=pos2)
            print("  [28/55] vm0 (rtos) echo double-echo ok (foreground)")

            # ---- virtio 读写全链(第十站):裸机对照组上验(站十六重锚 vm1) ----
            pos = len(buf)
            s.sendall(b"vmconsole 1\n")
            buf = read_until(s, buf, b"console -> vm", time.time() + 10, start=pos)
            p2 = len(buf)
            s.sendall(b"\n")
            buf = read_until(s, buf, b"vm1> ", time.time() + 10, start=p2)
            p = len(buf)
            s.sendall(b"dwrite\n")
            buf = read_until(s, buf, b"blk wrote", time.time() + 15, start=p)
            p = len(buf)
            s.sendall(b"dread\n")
            buf = read_until(s, buf, b"blk read: ESHINE-BLK-persist", time.time() + 15,
                             start=p)
            pos2 = len(buf)
            s.sendall(b"host\n")
            buf = read_until(s, buf, b"console -> host", time.time() + 10, start=pos2)
            print("  [29/55] virtio write/read + completion IRQ (3-desc chain -> vSPI 35)")

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
            print("  [30/55] vm1 echo double-echo ok (both alive, interleaved)")

            # ---- 抢占转写正确性:裸机 vm1 前台 spin 3G(站十六重锚),RTOS 并跑 ----
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
            s.sendall(b"vmconsole 1\n")
            buf = read_until(s, buf, b"console -> vm", time.time() + 10, start=pos)
            p2 = len(buf)
            s.sendall(b"\n")   # 空行:触发 guest 重打 prompt(唤醒不重打)
            buf = read_until(s, buf, b"vm1> ", time.time() + 10, start=p2)
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
            print("  [31/55] spin 3G survived %d quantum preemptions (frame migration intact)"
                  % (pe1 - pe0))

            # ---- WAIT 公平(站十六重锚 vm1):裸机睡,宿主活,后台醒 ----
            pos = len(buf)
            s.sendall(b"vmconsole 1\n")
            buf = read_until(s, buf, b"console -> vm", time.time() + 10, start=pos)
            p2 = len(buf)
            s.sendall(b"\n")   # 空行:触发 guest 重打 prompt(唤醒不重打)
            buf = read_until(s, buf, b"vm1> ", time.time() + 10, start=p2)
            p = len(buf)
            s.sendall(b"sleep\n")
            buf = read_until(s, buf, b"sleeping...", time.time() + 10, start=p)
            # 站十五:owner 撤 TIMER 自动归还后,睡客占台期间宿主夺回
            # 走逃生字符 0x1d(Ctrl-];裸机客另有 host HVC#4 自报)
            s.sendall(b"\x1d")
            buf = read_until(s, buf, b"console -> host (escape)",
                            time.time() + 10)
            buf = token_echo(s, buf, "FAIR%d" % os.getpid())
            print("  [32/55] vm1 WAIT -> host alive, rtos running (fairness)")

            buf = read_until(s, buf, b"[vm1] slept (vtimer irq)", time.time() + 20)
            print("  [33/55] vm1 woke in background rotation (WOKE->pump inject)")

            # ---- 双睡演变(站十六):裸机 one-shot 4s 与 RTOS 任务睡 2s 并存,
            # 醒序 = 后武者先醒——RTOS 周期 tick 与 vm1 单发共存,per-VM
            # vtimer ctx 若串台,vm1 会被 100Hz 重臂踩醒即翻案 ----
            pos = len(buf)
            s.sendall(b"vmconsole 1\n")
            buf = read_until(s, buf, b"console -> vm", time.time() + 10, start=pos)
            p2 = len(buf)
            s.sendall(b"\n")
            buf = read_until(s, buf, b"vm1> ", time.time() + 10, start=p2)
            p = len(buf)
            s.sendall(b"sleep 4\n")
            buf = read_until(s, buf, b"sleeping...", time.time() + 10, start=p)
            s.sendall(b"\x1d")
            buf = read_until(s, buf, b"console -> host (escape)", time.time() + 10)
            pos = len(buf)
            s.sendall(b"vmconsole 0\n")
            buf = read_until(s, buf, b"console -> vm", time.time() + 10, start=pos)
            p2 = len(buf)
            s.sendall(b"\n")
            buf = read_until(s, buf, b"rtos> ", time.time() + 10, start=p2)
            p = len(buf)
            s.sendall(b"sleep 2\n")
            buf = read_until(s, buf, b"sleeping...", time.time() + 10, start=p)
            s.sendall(b"\x1d")   # vm0 壳任务睡后夺回,唤醒走后台轮转
            buf = read_until(s, buf, b"console -> host (escape)", time.time() + 10)
            # start=p 锚死:[23] 已打过同款 slept needle,裸搜必中陈货(站八教训)
            buf = read_until(s, buf, b"[vm0] slept (rtos sleep)", time.time() + 15,
                             start=p)
            p_ord = buf.index(b"[vm0] slept (rtos sleep)") + 1
            buf = read_until(s, buf, b"[vm1] slept (vtimer irq)", time.time() + 15,
                             start=p_ord)   # 位置在后:醒序=后武装者先醒
            print("  [34/55] dual sleep: rtos tick-sleep (2s) beats bare-metal one-shot (4s); per-VM vtimer ctx")

            pos = len(buf)
            s.sendall(b"vmconsole 0\n")
            buf = read_until(s, buf, b"console -> vm", time.time() + 10, start=pos)
            p2 = len(buf)
            s.sendall(b"\n")
            buf = read_until(s, buf, b"rtos> ", time.time() + 10, start=p2)
            buf = token_echo(s, buf, "DUAL%d" % os.getpid())
            pos2 = len(buf)
            s.sendall(b"host\n")
            buf = read_until(s, buf, b"console -> host", time.time() + 10, start=pos2)
            print("  [35/55] both shells alive after dual sleep (rtos echo ok)")

            # ---- vblk 持久化(第九站,站十六重锚 vm1):销毁重建,数据仍在 ----
            pos = len(buf)
            s.sendall(b"vmconsole 1\n")
            buf = read_until(s, buf, b"console -> vm", time.time() + 10, start=pos)
            p2 = len(buf)
            s.sendall(b"\n")
            buf = read_until(s, buf, b"vm1> ", time.time() + 10, start=p2)
            p = len(buf)
            s.sendall(b"exit\n")
            buf = read_until(s, buf, b"vm1: VM_DEAD", time.time() + 15, start=p)
            pos = len(buf)
            s.sendall(b"vmcreate 1\n")
            buf = read_until(s, buf, b"vm1: CREATED", time.time() + 10, start=pos)
            s.sendall(b"vmrun 1\n")
            buf = read_until(s, buf, b"eshine> ", time.time() + 10, start=pos)
            buf = read_until(s, buf, b"[vm1] eshine-guest VM1 @EL1", time.time() + 20, start=pos)
            s.sendall(b"vmconsole 1\n")
            buf = read_until(s, buf, b"console -> vm", time.time() + 10, start=pos)
            p2 = len(buf)
            s.sendall(b"\n")
            buf = read_until(s, buf, b"vm1> ", time.time() + 10, start=p2)
            p = len(buf)
            s.sendall(b"dread\n")
            buf = read_until(s, buf, b"blk read: ESHINE-BLK-persist", time.time() + 15,
                             start=p)   # 上个生命周期的数据:静态磁盘不经复位
            print("  [36/55] virtio persistence: data survives destroy+recreate")

            # [36] 结束在 vm1 前台(dread 后未交还):宿主命令进不了
            # 自己的行编辑器(全入 vm1 RX 环)——先逃生夺回再前台化
            pos = len(buf)
            s.sendall(b"\x1d")
            buf = read_until(s, buf, b"console -> host (escape)", time.time() + 10,
                             start=pos)
            pos = len(buf)
            s.sendall(b"vmconsole 1\n")
            buf = read_until(s, buf, b"console -> vm", time.time() + 10, start=pos)
            p2 = len(buf)
            s.sendall(b"\n")   # 空行:触发 guest 重打 prompt(唤醒不重打)
            buf = read_until(s, buf, b"vm1> ", time.time() + 10, start=p2)
            p = len(buf)
            s.sendall(b"fault\n")
            buf = read_until(s, buf, b"fault: ESR=0x", time.time() + 10, start=p)
            buf = read_until(s, buf, b"vm1: faulted -> VM_DEAD", time.time() + 10, start=p)
            print("  [37/55] vm1 fault isolated: VM_DEAD, host alive")

            pos = len(buf)
            s.sendall(b"vmls\n")
            buf = read_until(s, buf, b"[0] VM_RUNNING", time.time() + 10, start=pos)
            buf = read_until(s, buf, b"[1] VM_DEAD", time.time() + 10, start=pos)
            buf = read_until(s, buf, b"eshine> ", time.time() + 10, start=pos)
            print("  [38/55] vmls: rtos RUNNING (kept for concurrency judge), bare-metal DEAD")

            prompt_pos = len(buf)
            token2 = "ESHINEOK%d" % (os.getpid() + 1)
            buf = token_echo(s, buf, token2)
            print("  [39/55] host shell intact after bare-metal lifecycle")

            # ---- guest 退场后 tick 仍增长(HCR/DAIF 恢复) ----
            buf, b20, a20 = tick_pair(s, buf, "post-guest tick")
            print("  [40/55] post-guest tick grows: %d -> %d (HCR_EL2_HOST/DAIF restore)"
                  % (b20, a20))

            pos = len(buf)
            s.sendall(b"vmkill 1\n")
            buf = read_until(s, buf, b"kill denied", time.time() + 10, start=pos)
            print("  [41/55] vmkill deny path ok (default-deny)")

            pos = len(buf)
            s.sendall(b"vmstat\n")
            buf = read_until(s, buf, b"[1] VM_DEAD", time.time() + 10, start=pos)
            print("  [42/55] vmstat still sane after teardown")

            # ---- 第十四站:Linux bring-up(槽 2,Image 外置 GPL 隔离)----
            pos = len(buf)
            s.sendall(b"vmcreate 2\n")
            buf = read_until(s, buf, b"vm2: CREATED", time.time() + 15, start=pos)
            p2 = len(buf)
            s.sendall(b"vmrun 2\n")
            buf = read_until(s, buf, b"Booting Linux on physical CPU", time.time() + 90, start=p2)
            p_boot = buf.index(b"Booting Linux on physical CPU", p2)   # 站十六:交错判据锚
            print("  [43/55] Linux enters via stage-2 + DTB x0 (arm64 boot protocol)")

            pos = len(buf)
            buf = read_until(s, buf, b"Machine model: eshine-os,vm", time.time() + 30)
            print("  [44/55] per-VM honest DTB: memory/cpus/timer nodes accepted")

            pos = len(buf)
            buf = read_until(s, buf, b"arch_timer: cp15 timer(s) running at 62.50MHz (virt)",
                             time.time() + 30)
            buf = read_until(s, buf, b"clocksource: arch_sys_counter", time.time() + 30)
            print("  [45/55] vtimer wiring (interrupt-names virt) + tick timekeeping")

            pos = len(buf)
            buf = read_until(s, buf, b"GICv3: CPU0: found redistributor 0",
                             time.time() + 30)
            print("  [46/55] Linux gic-v3 on emulated GICD/GICR (spec word decode)")

            # ---- 站十六:双客并发时序证明。窗口取"解掩蔽引导段":Linux
            # 早期 PSTATE.I=1,量子无从耗尽,泵单客模式直跑(活客短暂
            # 饿死系 guest 掩蔽所致,非宿主失职);local_irq_enable 后
            # 泵恢复轮转,活 RTOS 的心跳行与 boot 流交错即证两客真并发。
            # GICv3 针之后被动收 5s(泵 ~1s 一片,心跳两行/片,窗口余量足)。
            p_v3 = buf.rindex(b"GICv3: CPU0: found redistributor 0")
            end = time.time() + 5
            while time.time() < end:
                s.settimeout(0.5)
                try:
                    chunk = s.recv(4096)
                    if not chunk:
                        raise ConnectionError("serial closed during interleave window")
                    buf += chunk
                except socket.timeout:
                    pass
            if b"rtos: alive" not in buf[p_v3:]:
                raise AssertionError("no RTOS heartbeat inside Linux unmasked boot "
                                     "stream (dual-guest concurrency unproven): %r"
                                     % buf[p_v3:][-300:])
            n_hb = buf[p_v3:].count(b"rtos: alive")
            print("  [47/55] dual-guest concurrency: %d rtos heartbeats inside Linux boot" % n_hb)

            pos = len(buf)
            buf = read_until(s, buf, b"printk: console [ttyAMA0] enabled", time.time() + 60)
            print("  [48/55] PL011 full driver + console handover (AMBA probe)")

            pos = len(buf)
            buf = read_until(s, buf, b"virtio_blk virtio0: [vda] 128 512-byte logical blocks",
                             time.time() + 90)
            print("  [49/55] virtio-mmio legacy + vblk under Linux (completion IRQ chain)")

            # ---- 第十五站:initramfs 用户空间(busybox PID 1 shell)----
            pos = len(buf)
            buf = read_until(s, buf, b"Freeing unused kernel memory", time.time() + 60)
            buf = read_until(s, buf, b"Run /init as init process", time.time() + 30)
            print("  [50/55] initramfs chain: DTB initrd props -> unpack -> /init takes over")

            # 直落 Linux shell(owner 路由;RX 环->vSPI33->tty->sh 全链)
            pos = len(buf)
            s.sendall(b"vmconsole 2\n")
            buf = read_until(s, buf, b"console -> vm", time.time() + 30, start=pos)
            pos = len(buf)
            buf = read_until(s, buf, b"~ # ", time.time() + 30)
            print("  [51/55] busybox shell prompt (PID 1 userland via vmconsole)")

            pos = len(buf)
            token4 = "LINUX15TK%d" % (os.getpid() + 2)
            send_guest(s, ("echo " + token4 + "\n").encode())
            buf = read_until(s, buf, token4.encode(), time.time() + 30)
            # 竞态防:首中是打字回显,sh 输出在后——锚下一个 prompt 再计数
            p_after = buf.index(token4.encode(), pos) + len(token4)
            buf = read_until(s, buf, b"~ # ", time.time() + 30, start=p_after)
            if buf[pos:].count(token4.encode()) < 2:
                print("FAIL: guest shell token echo incomplete (terminal+sh expected twice)")
                print("raw tail: %r" % buf[pos:][-1500:])
                return 1
            print("  [52/55] shell token double echo (tty echo + sh output)")

            pos = len(buf)
            send_guest(s, b"cat /proc/cpuinfo\n")
            buf = read_until(s, buf, b"~ # ", time.time() + 30, start=pos)
            if b"CPU part\t: 0xd07" not in buf[pos:]:
                print("FAIL: /proc/cpuinfo missing cortex-a57 self-proof")
                print("raw tail: %r" % buf[pos:][-1200:])
                return 1
            print("  [53/55] userland self-proof: cat /proc/cpuinfo -> CPU part 0xd07")

            # ---- 站十六:RTOS 迟退(留活跨过双客并发判据后收场) ----
            pos = len(buf)
            s.sendall(b"\x1d")   # busybox 会话无 host HVC:逃生字符夺回
            buf = read_until(s, buf, b"console -> host (escape)",
                             time.time() + 15, start=pos)
            pos = len(buf)
            s.sendall(b"vmconsole 0\n")
            buf = read_until(s, buf, b"console -> vm", time.time() + 10, start=pos)
            p2 = len(buf)
            s.sendall(b"\n")
            buf = read_until(s, buf, b"rtos> ", time.time() + 10, start=p2)
            p = len(buf)
            s.sendall(b"exit\n")
            buf = read_until(s, buf, b"vm0: VM_DEAD", time.time() + 15, start=p)
            print("  [54/55] rtos exit -> VM_DEAD (HVC#1 face, RX auto-reclaimed)")

            pos = len(buf)
            s.sendall(b"vmkill 2\n")
            buf = read_until(s, buf, b"killed (host authority)", time.time() + 15, start=pos)
            buf, b21, a21 = tick_pair(s, buf, "post-linux tick")
            if a21 <= b21:
                print("FAIL: host tick frozen after Linux lifecycle")
                return 1
            token3 = "ESHINEOK%d" % (os.getpid() + 3)
            buf = token_echo(s, buf, token3)
            print("  [55/55] shell VM killed (authority); host intact (tick %d->%d, token echo)"
                  % (b21, a21))
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
