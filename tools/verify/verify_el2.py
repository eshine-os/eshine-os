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
销毁重建数据持久。门禁 67 步,
连续 2 轮才真绿。

第十八站(27.04)四块:A 块=vgic 在途拍守门(宿主相 27 三分支:
真唤醒才关线/在途回业主消费/无主收尸,[53] 闪断根治);B 块=互斥
单级优先级继承(pidemo 饥饿反转:mid 7 忙旋饿死 low 9,high 6 挂等
boost low 9->6 反超,9->6->9 全弧);C 块=per-task FP 帧(切换簿记
宏内嵌 SIMD 存取,fpdual 双 FP 任务并发双精确闭式自证);
D 块=vCPU>1(-smp 2 双核真并发:PSCI-SMC CPU_ON 唤起 CPU1 专泵跑
vm2,vm0/vm1 留 CPU0;VMPIDR 虚拟化=客恒见 cpu0;per-CPU
excp_stack/cur_vcpu/GICR;console 自旋锁;WFE/SEV 邮箱;VM 状态锁)。
新判据×4:[3] cpu1 online、[30] pidemo 反超序、[31] fpdual 双精确、
[50] 真并行硬证(vm1 spin 独占 CPU0 的窗口内 vm2 内核行持续行进,
单物理 CPU 下不可能)。

第十六站(27.04):VM0 槽升格为 eshine-rtos(参考RTOS 公开设计为基准的
最小任务内核:32 级位图抢占+tick 100Hz+IRQ 驱动壳);guest1.bin 退役,
guest2.bin(裸机)留任 VM1 对照组;SIMD per-VM 帧入泵(双 FP 客并跑
前置)。新判据:rtos up/ps/spawn/fp 自证/sleep 心跳夹行/yield/双客
并发时序证明(心跳行夹在 Linux boot 行流之间)+ vm0 exit 迟退步;
旧 vm0 裸机系判据(dwrite/dread/spin/WAIT 公平/持久化)全数重锚
vm1 同强度;双睡判据演进为"RTOS 周期 tick 与裸机 one-shot 并存
醒序"(per-VM vtimer ctx 更强形态)。

第十七站(27.04):eshine-rtos 同步原语(每对象 FIFO 等待队列的
计数信号量+owner/递归互斥,参考RTOS 公开设计为基准;DAIF 短临界段,
唤醒即抢占)。新判据×3:semdemo 有界环 8/8(信号量对阻塞乒乓,
per-item 计数 8+8)、mutexdemo ps mid-hold(waiter BLOCK/contender
SLEEP——阻塞是 ps 表里的活状态,非打出来的日志)、mutexdemo 醒序
(unlocking < locked-after-block)+递归 take 2+非拥有者 give 拒绝。
存量 55 步原样保留,新判据插 [26-28](vm0 独占段),旧 [26-55] 顺移。
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


_HB_RE = re.compile(rb"rtos: alive tick=\d+\r?\n")


def token_echo(s, buf, tok, deadline=10):
    """发送 echo <tok>,循环等 token 出现两次(回显+输出);返回 buf。
    注意不能用 read_until(\n) 两连:第一个 \n 在旧数据里,第二次匹配
    会立即返回而不收新输出(实测踩过)。
    站20 [41] 定谳:计数对心跳行鲁棒。vm0 前台期心跳(0.5s 一拍,
    合法并发输出)恰落型键回显段中间时,token 字节被劈成两半
    ("echo GOK"+心跳行+"A41226"),朴素 count 把健康客判成 token
    单见闪断([37] 三连"失败"全息画像:回显劈半/输出完整/提示符在,
    rrx/vuart 环/LR1 三面皆空,壳 BLOCK 等 RX 是健康息态)。剥除
    心跳行再计,劈半即愈合;双回显语义不变,真死客(计数仍 <2)
    照样必炸。"""
    p = len(buf)
    # 站十四:命令分块发——QEMU pl011 RX FIFO 16 字节,guest 在跑时宿主
    # 排空慢,一次性 sendall 长命令会丢尾(Linux panic 后实测)。
    cmd = ("echo " + tok + "\n").encode()
    for i in range(0, len(cmd), 8):
        s.sendall(cmd[i:i + 8])
        time.sleep(0.03)
    end = time.time() + deadline
    while _HB_RE.sub(b"", buf[p:]).count(tok.encode()) < 2:
        if time.time() > end:
            raise AssertionError("token %r seen %d time(s) after strip\n"
                                 "raw tail: %r"
                                 % (tok,
                                    _HB_RE.sub(b"", buf[p:]).count(tok.encode()),
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
            "-smp", "4",                 # 站十九D 三核+站二十 CPU3(vm0.vcpu1 专泵)
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
            print("  [1/67] banner ok (" + VER + ")")
            buf = read_until(s, buf, b"eshine> ", time.time() + 10)
            print("  [2/67] shell prompt ok")

            # ---- 站十八D:CPU1 起立(PSCI CPU_ON;上线打印在提示符前握手)----
            if b"cpu1: online (psci cpu_on)" not in buf:
                raise AssertionError("cpu1 online line missing: %r" % buf[-300:])
            print("  [3/67] cpu1: online (psci cpu_on) — secondary core up")
            prompt_pos = len(buf)
            token = "ESHINEOK%d" % os.getpid()
            buf = token_echo(s, buf, token)
            print("  [4/67] host token double-echo ok")

            # ---- A 相:真中断判据 ----
            buf, b4, a4 = tick_pair(s, buf, "host tick")
            print("  [5/67] tick grows: %d -> %d (CNTHP->GICv3->EL2 IRQ)" % (b4, a4))

            pos = len(buf)
            s.sendall(b"vmls\n")
            buf = read_until(s, buf, b"[0] VM_INIT", time.time() + 10, start=pos)
            buf = read_until(s, buf, b"[1] VM_INIT", time.time() + 10, start=pos)
            print("  [6/67] vmls: empty table")

            pos = len(buf)
            s.sendall(b"vmcreate 0\n")
            buf = read_until(s, buf, b"vm0: CREATED", time.time() + 10, start=pos)
            print("  [7/67] vmcreate 0 -> CREATED")

            # ---- vmconsole 状态门:非 RUNNING 不可前台化(默认拒绝)。
            # owner 指给不消费输入的 VM = 终端永久锁死(输入全入环无人
            # 读,vmconsole host 自救同样被路由)——实锤修复的回归判据。 ----
            pos = len(buf)
            s.sendall(b"vmconsole 0\n")
            buf = read_until(s, buf, b"denied: not running", time.time() + 10, start=pos)
            print("  [8/67] vmconsole state gate: denied before vmrun")

            # ---- VM0:异步启动(后台跑动,banner 经 vuart 陷阱整行前缀冲) ----
            # 站十六:VM0 槽 = eshine-rtos(任务内核客);banner 即 RTOS 身份
            pos = len(buf)
            s.sendall(b"vmrun 0\n")
            buf = read_until(s, buf, b"eshine> ", time.time() + 10, start=pos)
            buf = read_until(s, buf, ("[vm0] eshine-rtos " + VER).encode(),
                             time.time() + 20, start=pos)   # 后台 banner:[vmN] 前缀判据(第六站)
            print("  [9/67] vmrun 0 async -> rtos banner via vuart ([vm0] prefix)")

            # ---- DTB 自查(第十一站):三设备身份从注入树查出(非硬编码) ----
            buf = read_until(s, buf, b"[vm0] fdt ok: cpu arm,cortex-a57",
                             time.time() + 10, start=pos)
            print("  [10/67] fdt self-check: cpu node (arm,cortex-a57)")
            buf = read_until(s, buf, b"[vm0] fdt ok: uart arm,pl011",
                             time.time() + 10, start=pos)
            print("  [11/67] fdt self-check: uart node (arm,pl011)")
            buf = read_until(s, buf, b"[vm0] fdt ok: blk virtio,mmio",
                             time.time() + 10, start=pos)
            print("  [12/67] fdt self-check: blk node (virtio,mmio)")

            # ---- PL011 完整化(第十三站):AMBA 身份证+开机(CR 真语义) ----
            buf = read_until(s, buf, b"[vm0] pl011 on: amba 00141011",
                             time.time() + 10, start=pos)
            print("  [13/67] pl011 amba id 0x00141011 (PERIPHID/PCELLID probe)")

            # ---- vGIC 真启动式初始化(第十二站):distributor 成为注入权威 ----
            buf = read_until(s, buf, b"[vm0] gicd v3 ok", time.time() + 10,
                             start=pos)
            print("  [14/67] gicd v3 probe via emulated GICD (PIDR2 == 0x30)")
            buf = read_until(s, buf, b"[vm0] gic on: 27 33 35", time.time() + 10,
                             start=pos)
            print("  [15/67] gic enabled via GICD/GICR (ppi27 + spi33/35 readback)")

            # ---- UART IRQ 模式(第八站):guest 使能 RXIM 后自报 ----
            buf = read_until(s, buf, b"[vm0] uart irq on", time.time() + 10,
                             start=pos)
            print("  [16/67] guest enabled RXIM -> uart irq mode (vSPI 33 path)")

            # ---- virtio 就绪(第十站):probe 三验+队列+DRIVER_OK 自报 ----
            buf = read_until(s, buf, b"[vm0] virtio blk on", time.time() + 10,
                             start=pos)
            print("  [17/67] virtio probe + DRIVER_OK -> virtio blk on (vSPI 35 path)")

            # ---- 站十六:RTOS 世界起立(位图调度器+tick+任务) ----
            buf = read_until(s, buf, b"[vm0] rtos: up (eshine-rtos", time.time() + 10,
                             start=pos)
            print("  [18/67] rtos up: 32-prio bitmap scheduler, tick 100Hz")

            # ---- RX 唤醒链:前台化,空行入环 -> 直通唤醒 -> FR 命中 ----
            pos = len(buf)
            s.sendall(b"vmconsole 0\n")
            buf = read_until(s, buf, b"console -> vm", time.time() + 10, start=pos)
            p2 = len(buf)
            s.sendall(b"\n")   # RTOS 壳任务 BLOCK 中:入环即唤醒,重打 prompt
            # 站20:锚取"回显+提示符"成对(CRLF 是壳对本 '\n' 的回显)。
            # vmconsole 切换随带冲刷的启动期半行是 "[vm0] rtos> "(带
            # tag,CRLF 后跟 '[' 不匹配),裸 "rtos> " 锚会误吞它,整条
            # 判据链错位一格([21] "ps 行缺失"闪断即此,实测全息画像)。
            # 配对锚只认本 '\n' 换来的真提示符。
            buf = read_until(s, buf, b"\r\nrtos> ", time.time() + 20, start=p2)
            print("  [19/67] RX wake chain: push -> irq -> shell task READY -> prompt")

            p = len(buf)
            s.sendall(b"selftest\n")
            buf = read_until(s, buf, b"selftest OK", time.time() + 20, start=p)
            buf = read_until(s, buf, b"rtos> ", time.time() + 10, start=p)
            print("  [20/67] VM0 (rtos) selftest OK (x19/x30 live across 100 MMIO traps)")

            # ---- 站十六:ps——任务表活体(shell/heartbeat/idle 三行在场) ----
            p = len(buf)
            s.sendall(b"ps\n")
            buf = read_until(s, buf, b"rtos> ", time.time() + 10, start=p)
            for row in (b"shell", b"heartbeat", b"idle"):
                if row not in buf[p:]:
                    raise AssertionError("ps missing row %r: %r" % (row, buf[p:][-200:]))
            print("  [21/67] ps: shell/heartbeat/idle rows (task table live)")

            # ---- 站十六:spawn——动态任务生-问候-亡全链 ----
            p = len(buf)
            send_guest(s, b"spawn gate16\n")
            buf = read_until(s, buf, b"gate16: hello from task", time.time() + 15, start=p)
            buf = read_until(s, buf, b"rtos> ", time.time() + 10, start=p)
            print("  [22/67] spawn gate16 -> born, greeted, exited to pool")

            # ---- 站十六:fp 自证——1e6 double 和跨任务/跨客切换精确 ----
            p = len(buf)
            s.sendall(b"fp\n")
            buf = read_until(s, buf, b"rtos> ", time.time() + 10, start=p)   # 壳立回(任务异步)
            buf = read_until(s, buf, b"fp OK: 500000500000", time.time() + 60, start=p)
            print("  [23/67] fp self-proof: 1e6 double sum exact (SIMD frame across VM/task switches)")

            # ---- 站十六:壳任务睡 3s,心跳任务不睡——窗口内必有 alive 行 ----
            p = len(buf)
            s.sendall(b"sleep 3\n")
            buf = read_until(s, buf, b"sleeping...", time.time() + 10, start=p)
            buf = read_until(s, buf, b"slept (rtos sleep)", time.time() + 20, start=p)
            p_end = buf.index(b"slept (rtos sleep)", p)
            if b"rtos: alive" not in buf[p:p_end]:
                raise AssertionError("no heartbeat during shell sleep: %r" % buf[p:p_end][-200:])
            print("  [24/67] shell sleep 3s: heartbeat interleaved (task-level concurrency)")

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
            print("  [25/67] HVC#2 yield -> PAUSED -> vmrun resume -> '[vm0] resumed' (task ctx intact)")

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
            print("  [26/67] HVC#4 handback + vmstat readable (preempts=%s so far)"
                  % m.group(1).decode())

            # ---- 站十七:同步原语三判据(vm0 独占段,前台重取) ----
            pos = len(buf)
            s.sendall(b"vmconsole 0\n")
            buf = read_until(s, buf, b"console -> vm", time.time() + 10, start=pos)
            p2 = len(buf)
            s.sendall(b"\n")       # 空行:入环唤醒重打 prompt
            buf = read_until(s, buf, b"rtos> ", time.time() + 10, start=p2)

            # [26] semdemo:有界环生产者-消费者(信号量对,阻塞乒乓)
            p = len(buf)
            s.sendall(b"semdemo\n")
            buf = read_until(s, buf, b"semdemo: ring done (8/8)", time.time() + 20,
                             start=p)
            if buf[p:].count(b"semdemo: p") != 8 or buf[p:].count(b"semdemo: c") != 8:
                raise AssertionError("semdemo item counts wrong (p=%d c=%d): %r"
                                     % (buf[p:].count(b"semdemo: p"),
                                        buf[p:].count(b"semdemo: c"),
                                        buf[p:][-200:]))
            print("  [27/67] semdemo: bounded ring 8/8 (sem pair, blocking ping-pong)")

            # [27] mutexdemo 上半场:ps mid-hold——阻塞是 ps 表里的活状态
            p = len(buf)
            p_md = p                    # 站20:全程锚——waiter 首片可能
                                        # 跑在 ps 命令处理之前,needle 检查
                                        # 若只看 ps 后窗口会假阴性(-smp 4
                                        # 时序实测);醒序判据仍用 ps 后窗口
            s.sendall(b"mutexdemo\n")
            buf = read_until(s, buf, b"mutex: contender locked", time.time() + 10,
                             start=p)
            p = len(buf)
            s.sendall(b"ps\n")
            buf = read_until(s, buf, b"rtos> ", time.time() + 10, start=p)
            if b"waiter prio 8  BLOCK" not in buf[p:] or \
               b"contender prio 6  SLEEP" not in buf[p:]:
                raise AssertionError("ps mid-hold rows missing (waiter BLOCK/"
                                     "contender SLEEP): %r" % buf[p:][-300:])
            print("  [28/67] ps mid-hold: waiter BLOCK on mutex, contender SLEEP (live state)")

            # [28] mutexdemo 下半场:醒序+递归+非拥有者拒绝
            buf = read_until(s, buf, b"mutex: waiter done", time.time() + 20, start=p)
            i = buf.index(b"mutex: contender unlocking")
            if buf.index(b"mutex: waiter locked (after block)", i) <= i:
                raise AssertionError("waiter locked before contender unlock: %r"
                                     % buf[p:][-300:])
            if b"mutex: give denied (not owner)" not in buf[p_md:] or \
               b"mutex: recursive take 2 ok" not in buf[p_md:]:
                raise AssertionError("mutex denial/recursion needles missing: %r"
                                     % buf[p:][-300:])
            print("  [29/67] mutex: block-wake order + recursion + non-owner denial")

            # ---- 站十八B:pidemo——PI 饥饿反转(mid 忙旋饿死 low,high 挂等 boost 反超)----
            p = len(buf)
            s.sendall(b"pidemo\n")
            buf = read_until(s, buf, b"pi: low restored prio 9", time.time() + 20, start=p)
            buf = read_until(s, buf, b"pi: mid done", time.time() + 15, start=p)
            a_ = buf.index(b"pi: mid spinning", p)
            if buf.index(b"pi: low done (boosted)", p) < a_:
                raise AssertionError("low done before mid spin (PI proof broken): %r"
                                     % buf[p:][-300:])
            for needle in (b"pi: low in CS at prio 9",
                           b"pi: low running at prio 6 (boosted)",
                           b"pi: high got lock (after boost)"):
                if needle not in buf[p:]:
                    raise AssertionError("pidemo needle missing %r: %r"
                                         % (needle, buf[p:][-300:]))
            print("  [30/67] pidemo: PI starvation reversal (low 9->6->9, high got lock)")

            # ---- 站十八C:fpdual——per-task FP 双并发自证 ----
            p = len(buf)
            s.sendall(b"fpdual\n")
            buf = read_until(s, buf, b"fpdual: B OK: 250000250000", time.time() + 30, start=p)
            if b"fpdual: A OK: 500000500000" not in buf[p:]:
                raise AssertionError("fpdual A missing: %r" % buf[p:][-300:])
            print("  [31/67] fpdual: dual FP tasks both exact (per-task SIMD frame)")

            # ---- 站十九B:事件组——AND 双拍不醒+双拍齐醒+OR 单拍即醒 ----
            p = len(buf)
            s.sendall(b"eventdemo\n")
            buf = read_until(s, buf, b"evt: waiter fired flags=0x5", time.time() + 30, start=p)
            buf = read_until(s, buf, b"evt: OR fired flags=0x8", time.time() + 15, start=p)
            a_ = buf.index(b"evt: waiter fired flags=0x5", p)
            if buf.index(b"evt: A set (waiter stays)", p) > a_:
                raise AssertionError("AND waiter woke before both bits: %r" % buf[p:][-300:])
            print("  [32/67] eventdemo: AND needs both bits, OR wakes on single")

            # ---- 站十九B:消息队列——16 条环 + 满发阻塞 ----
            p = len(buf)
            s.sendall(b"queuedemo\n")
            buf = read_until(s, buf, b"q: ring full, sender blocked", time.time() + 20, start=p)
            buf = read_until(s, buf, b"q: done 16/16", time.time() + 20, start=p)
            if buf[p:].count(b"q: sent ") < 16 or buf[p:].count(b"q: got ") < 16:
                raise AssertionError("queue 16-msg chain incomplete: %r" % buf[p:][-300:])
            print("  [33/67] queuedemo: 16 msgs ring + full-blocking handoff")

            # ---- 站十九C:per-task FP lazy——M(fp-saves) << N(switches) ----
            p = len(buf)
            s.sendall(b"fpstat\n")
            buf = read_until(s, buf, b"fp-saves", time.time() + 15, start=p)
            m = re.search(rb"lazy: (\d+) switches, (\d+) fp-saves", buf[p:])
            if not m:
                raise AssertionError("fpstat format: %r" % buf[p:][-120:])
            n_sw, n_sv = int(m.group(1)), int(m.group(2))
            if not (0 < n_sv < n_sw):
                raise AssertionError("fpstat M<<N broken: %d saves / %d switches"
                                     % (n_sv, n_sw))
            print("  [34/67] fpstat: lazy FP %d saves << %d switches" % (n_sv, n_sw))

            pos = len(buf)
            s.sendall(b"host\n")     # 交还宿主(VM1 段从宿主语境起)
            buf = read_until(s, buf, b"console -> host", time.time() + 10, start=pos)

            # ---- VM1:裸机对照组上场(站十六:guest2.bin 留任) ----
            pos = len(buf)
            s.sendall(b"vmcreate 1\n")
            buf = read_until(s, buf, b"vm1: CREATED", time.time() + 10, start=pos)
            s.sendall(b"vmrun 1\n")
            buf = read_until(s, buf, b"eshine> ", time.time() + 10, start=pos)
            buf = read_until(s, buf, b"[vm1] eshine-guest VM1 @EL1", time.time() + 20,
                             start=pos)   # 后台 banner 前缀判据(vm1 同律)
            print("  [35/67] vmrun 1 async -> bare-metal banner via vuart ([vm1] prefix)")
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
            print("  [36/67] both RUNNING (preempts=%s so far)" % m.group(1).decode())

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
            print("  [37/67] vm0 (rtos) echo double-echo ok (foreground)")

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
            print("  [38/67] virtio write/read + completion IRQ (3-desc chain -> vSPI 35)")

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
            print("  [39/67] vm1 echo double-echo ok (both alive, interleaved)")

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
            print("  [40/67] spin 3G survived %d quantum preemptions (frame migration intact)"
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
            print("  [41/67] vm1 WAIT -> host alive, rtos running (fairness)")

            buf = read_until(s, buf, b"[vm1] slept (vtimer irq)", time.time() + 20)
            print("  [42/67] vm1 woke in background rotation (WOKE->pump inject)")

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
            print("  [43/67] dual sleep: rtos tick-sleep (2s) beats bare-metal one-shot (4s); per-VM vtimer ctx")

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
            print("  [44/67] both shells alive after dual sleep (rtos echo ok)")

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
            print("  [45/67] virtio persistence: data survives destroy+recreate")

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
            print("  [46/67] vm1 fault isolated: VM_DEAD, host alive")

            pos = len(buf)
            s.sendall(b"vmls\n")
            buf = read_until(s, buf, b"[0] VM_RUNNING", time.time() + 10, start=pos)
            buf = read_until(s, buf, b"[1] VM_DEAD", time.time() + 10, start=pos)
            buf = read_until(s, buf, b"eshine> ", time.time() + 10, start=pos)
            print("  [47/67] vmls: rtos RUNNING (kept for concurrency judge), bare-metal DEAD")

            prompt_pos = len(buf)
            token2 = "ESHINEOK%d" % (os.getpid() + 1)
            buf = token_echo(s, buf, token2)
            print("  [48/67] host shell intact after bare-metal lifecycle")

            # ---- guest 退场后 tick 仍增长(HCR/DAIF 恢复) ----
            buf, b20, a20 = tick_pair(s, buf, "post-guest tick")
            print("  [49/67] post-guest tick grows: %d -> %d (HCR_EL2_HOST/DAIF restore)"
                  % (b20, a20))

            pos = len(buf)
            s.sendall(b"vmkill 1\n")
            buf = read_until(s, buf, b"kill denied", time.time() + 10, start=pos)
            print("  [50/67] vmkill deny path ok (default-deny)")

            pos = len(buf)
            s.sendall(b"vmstat\n")
            buf = read_until(s, buf, b"[1] VM_DEAD", time.time() + 10, start=pos)
            print("  [51/67] vmstat still sane after teardown")

            # ---- 第十四站:Linux bring-up(槽 2,Image 外置 GPL 隔离)----
            pos = len(buf)
            s.sendall(b"vmcreate 2\n")
            buf = read_until(s, buf, b"vm2: CREATED", time.time() + 15, start=pos)
            p2 = len(buf)
            s.sendall(b"vmrun 2\n")
            buf = read_until(s, buf, b"Booting Linux on physical CPU", time.time() + 90, start=p2)
            p_boot = buf.index(b"Booting Linux on physical CPU", p2)   # 站十六:交错判据锚
            print("  [52/67] Linux enters via stage-2 + DTB x0 (arm64 boot protocol)")

            # ---- 站十九D:guest SMP——第二 vCPU 上线(Linux 引导真行;
            #      设计稿所引 "Bringing up 1 secondary CPU(s)" 实测不存在,
            #      以真行为准,spec §8 纠偏)----
            pos = len(buf)
            buf = read_until(s, buf, b"smp: Brought up 1 node, 2 CPUs", time.time() + 120)
            if b"CPU1: Booted secondary processor" not in buf[pos:]:
                raise AssertionError("secondary vCPU never booted (SMP broken): %r"
                                     % buf[pos:][-300:])
            print("  [53/67] guest SMP: secondary vCPU up (Brought up 1 node, 2 CPUs)")

            # ---- 站十八D:真并行硬证——CPU0 自旋独占窗内,CPU1 上的 vm2 持续行进 ----
            # 单物理 CPU 下 spin 3G 饿死一切他客;vm2 内核行在窗内继续
            # 前进(时间戳单调)唯一解释:第二物理核在跑它。
            pos = len(buf)
            s.sendall(b"vmcreate 1\n")
            buf = read_until(s, buf, b"vm1: CREATED", time.time() + 10, start=pos)
            p2 = len(buf)
            s.sendall(b"vmrun 1\n")
            buf = read_until(s, buf, b"eshine> ", time.time() + 10, start=p2)
            s.sendall(b"vmconsole 1\n")
            buf = read_until(s, buf, b"console -> vm", time.time() + 10, start=p2)
            p2 = len(buf)
            s.sendall(b"\n")
            buf = read_until(s, buf, b"vm1> ", time.time() + 15, start=p2)
            p2 = len(buf)
            s.sendall(b"spin\n")
            buf = read_until(s, buf, b"spinning...", time.time() + 15, start=p2)
            a_ = buf.index(b"spinning...", p2) + len(b"spinning...")
            buf = read_until(s, buf, b"spun 3G iters (preempted ok)", time.time() + 60,
                             start=a_)
            window = buf[a_:buf.index(b"spun 3G iters", a_)]
            stamps = re.findall(rb"\[vm2\] \[\s*([0-9]+\.[0-9]+)\]", window)
            ts = [float(x) for x in stamps]
            if len(ts) < 3 or ts != sorted(ts):
                raise AssertionError("parallel proof failed: %d vm2 lines in spin window: %r"
                                     % (len(ts), window[:400]))
            print("  [54/67] true parallelism: %d vm2 boot lines advanced inside vm1 spin (2nd physical CPU)"
                  % len(ts))
            p2 = len(buf)
            s.sendall(b"host\n")          # 交还宿主:后续并发判据窗口要有 vm0 心跳
            buf = read_until(s, buf, b"console -> host", time.time() + 10, start=p2)
            # 站20:心跳前台门控后,并发判据窗口须 vm0 前台化才有
            # 心跳行(站十九前 vmrun 后台即印——用户令改,免扰宿主/
            # 他客串口);vm2 靴流后台照常(tag 行不受影响)
            p2 = len(buf)
            s.sendall(b"vmconsole 0\n")
            buf = read_until(s, buf, b"console -> vm", time.time() + 10, start=p2)

            pos = len(buf)
            buf = read_until(s, buf, b"Machine model: eshine-os,vm", time.time() + 30)
            print("  [55/67] per-VM honest DTB: memory/cpus/timer nodes accepted")

            pos = len(buf)
            buf = read_until(s, buf, b"arch_timer: cp15 timer(s) running at 62.50MHz (virt)",
                             time.time() + 30)
            buf = read_until(s, buf, b"clocksource: arch_sys_counter", time.time() + 30)
            print("  [56/67] vtimer wiring (interrupt-names virt) + tick timekeeping")

            pos = len(buf)
            buf = read_until(s, buf, b"GICv3: CPU0: found redistributor 0",
                             time.time() + 30)
            print("  [57/67] Linux gic-v3 on emulated GICD/GICR (spec word decode)")

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
            print("  [58/67] dual-guest concurrency: %d rtos heartbeats inside Linux boot" % n_hb)

            pos = len(buf)
            buf = read_until(s, buf, b"printk: console [ttyAMA0] enabled", time.time() + 60)
            print("  [59/67] PL011 full driver + console handover (AMBA probe)")

            pos = len(buf)
            buf = read_until(s, buf, b"virtio_blk virtio0: [vda] 128 512-byte logical blocks",
                             time.time() + 90)
            print("  [60/67] virtio-mmio legacy + vblk under Linux (completion IRQ chain)")

            # ---- 第十五站:initramfs 用户空间(busybox PID 1 shell)----
            pos = len(buf)
            buf = read_until(s, buf, b"Freeing unused kernel memory", time.time() + 60)
            buf = read_until(s, buf, b"Run /init as init process", time.time() + 30)
            print("  [61/67] initramfs chain: DTB initrd props -> unpack -> /init takes over")

            # 直落 Linux shell(owner 路由;RX 环->vSPI33->tty->sh 全链)
            pos = len(buf)
            s.sendall(b"\x1d")   # 站20:vm0 前台期(心跳窗),先逃生夺回
            buf = read_until(s, buf, b"console -> host (escape)", time.time() + 15,
                             start=pos)
            pos = len(buf)
            s.sendall(b"vmconsole 2\n")
            buf = read_until(s, buf, b"console -> vm", time.time() + 30, start=pos)
            pos = len(buf)
            buf = read_until(s, buf, b"~ # ", time.time() + 30)
            print("  [62/67] busybox shell prompt (PID 1 userland via vmconsole)")

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
            print("  [63/67] shell token double echo (tty echo + sh output)")

            pos = len(buf)
            send_guest(s, b"cat /proc/cpuinfo\n")
            buf = read_until(s, buf, b"~ # ", time.time() + 30, start=pos)
            if b"CPU part\t: 0xd07" not in buf[pos:]:
                print("FAIL: /proc/cpuinfo missing cortex-a57 self-proof")
                print("raw tail: %r" % buf[pos:][-1200:])
                return 1
            print("  [64/67] userland self-proof: cat /proc/cpuinfo -> CPU part 0xd07")

            # ---- 站十九D:双核用户态自证——guest 内第二处理器可见 ----
            pos = len(buf)
            send_guest(s, b"cat /proc/cpuinfo\n")
            buf = read_until(s, buf, b"processor\t: 1", time.time() + 30, start=pos)
            buf = read_until(s, buf, b"~ # ", time.time() + 15, start=pos)
            print("  [65/67] userland SMP self-proof: cpuinfo -> processor 1 (2nd vCPU)")

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
            print("  [66/67] rtos exit -> VM_DEAD (HVC#1 face, RX auto-reclaimed)")

            pos = len(buf)
            s.sendall(b"vmkill 2\n")
            buf = read_until(s, buf, b"killed (host authority)", time.time() + 15, start=pos)
            buf, b21, a21 = tick_pair(s, buf, "post-linux tick")
            if a21 <= b21:
                print("FAIL: host tick frozen after Linux lifecycle")
                return 1
            token3 = "ESHINEOK%d" % (os.getpid() + 3)
            buf = token_echo(s, buf, token3)
            print("  [67/67] shell VM killed (authority); host intact (tick %d->%d, token echo)"
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
