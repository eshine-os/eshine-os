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
销毁重建数据持久。门禁 81 步
(站28:+utest EL0 syscall 面,[77-80] 顺移 [78-81]),
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

第二十站(27.04)SMP RTOS:-smp 4 拓扑(vm_cpu_map={0,3}/{0,0}/
{1,2},vm0.vcpu1 上 cpu3 专泵);RTOS per-CPU 就绪队列+自旋锁+SGI
重调度(rt_ipi=ICC_SGI1R 写,EL2 陷阱→vgic 帧挂起→poke→泵注入);
PSCI CPU_ON 泛化随 n_vcpu 几何表(vgic nframes 同随,rx=0 三层定谳
的第三层)。新判据×5(插 [34] 后,旧 [35-67] 顺移 +5):[35] 次核
自报+帧 1 使能回读 0x08000001、[36] smpdemo 双核行交错、
[37] ipidemo SGI 往返>=20+跨核唤醒 3、[38] fpsmp per-CPU FP 双精确、
[39] 次核在线主核不回归(ps+fpstat 两核求和递增)。

第二十一站(27.04)三块合成站(A 同步超时/B panic 观测/C 栈防护;
对抗评审 16 条 CONFIRMED 全折入后实施):超时族按 参考RTOS 公开语义
(0=no-wait,RT_WAIT_FOREVER=永不超时),take 环单窗化(复核+登记
+挂链+摘 rdy 同一 DAIF 窗——两窗 gap=丢醒窗,评审核);扫描摘除+
owner PI 重算(等待集变化不变量,v1"零改动"断言翻案);栈行=垫+
哨兵+水位,溢出受控越哨后解栈回浅再 wfi(异常帧永不在垫区)。
新判据×3:[40] tmodemo 超时全弧+PI 14->9->14 序断、[41] stkdemo
水位+哨兵+ps 水位列、[74] overflow→rt_panic 指名(vm0 exit 后
复活链 vmcreate/vmrun/vmconsole,vm2 仍在场)。号段:旧 [40-71] 顺移 +2=[42-73],
overflow 新门 [74] 插在终局前,旧终局 [72] 再让位至 [75]。

第二十二站(27.04)A=[54] 四站悬案定谳:B 块前先修——R1b dump 取证
needle 是字节级撕裂而非缺席("spinni[vm2] [   n 0.1339g17]",vm1
与 vm2 输出互嵌);根因=console_putc 原语不持锁(vuart fg 逐字符
透传为唯一高频裸写者)+ fg 字符缝隙可嵌整行;修=原语按 owner 递归
持锁 + fg 行缓冲化('
' 锁内整行原子)+ 泊位冲刷(爆发感知 + 全
VM 静默判——第二雷:per-VM tx 缓冲被同 VM 他帧共享,vcpu0 泊位冲
刷会切断 vcpu1 在途行,"[vm0] rtos> c[vm0] pu1: online"门禁自抓
实测定谳)。probe-tear 失败测试先行(修复前 60 行风暴 4+ 撕→修复
后输出行 0/60 碎)。B=软定时器:rt_timer_t 静态池(指针登记,8 槽),
核 0 tick-ISR 回调语境(puts_lk 持锁即屏本核=ISR 安全契约),零漂
移周期重臂;新判据 [42] swtdemo 三面(periodic 1/2/3+自停+单发+
取消缺席),旧 [42-75] 顺移 +1 → 76 门。

第二十三站(27.04)三块:irq 注册表(四硬编码分支退役,替换式
登记+链式包装,irqdemo 换 33 号槽计数/恢复,探针 x 0.1s 延迟补
发供计数——命令字节在换钩前已被壳消费,首版机理为假)/水位守卫
(live-sp 判——tick 守卫读的是 ISR 链下方 sp,告警线=预算+读点差;
lowdemo 贴线深旋恰一次 warn,magic 不误伤)/console 延迟冲刷(泊位
武装 CNTPCT+20ms,泵入口扫描+fire 时刻复验静默判,时间基=CPU0
宿主壳忙自旋非 vm0 tick;chardev 分块缝吸收,[54] 残余收编)。
号段:[43][44] 插 swtdemo 后;echo 风暴门终落真并行窗 [65]
(vm2 靴流内 vm1 前台)——评审迁窗定谳:初版 [51] 落点 owner=host
且 vm2 未建,needle 由宿主壳自产,门恒绿空转。79 门。
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
    # 站23C 修真:撤站十四分块(为前 vuart 时代的 16B RX FIFO 补丁)
    # ——今 RX 走 64B vuart 软环,16-20B 命令单发零丢失(整行直发
    # 探针实证)。且 30ms 人工块隙 > 泊位延迟窗(20ms),分块使打字
    # 回显在块间隙被分段冲刷,互插者([vm1] 靴行)插缝,HB-strip
    # 不愈合非心跳行 → token 单见假红(R-c/d [47] 双定谳)。
    cmd = ("echo " + tok + "\n").encode()
    s.sendall(cmd)
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


_FOREN_S = None    # 站25:门红自动取证的套接字引用(main 建连后挂上)


def _forensic_dump():
    """站25:任何门红=自动取证——逃逸回宿主,vmstat/vmrx/vmtrail 512
    落日志后原样抛出。这覆盖**全类失败脸**:[41] 家族无论以何脸
    复发(泊定类/运行态 tick 停发类/输入类),门红即证据到手
    (16K 环 ≈27s 窗)。失败现场不再随 buf 丢弃。
    站28 vtimer 专站修真:旧版单层 try/except 吞异常连 print 一起
    吞(两轮门红全证物灭失,复盘定谳)——改分段守卫,每段落袋
    即印;另加双 vmstat(间隔 4s,宿主 tick 存活直接判)+ gdb 双
    取证(_forensic_gdb:四核定时器组/GICR active/pending 位/宿主
    freq,ticks 静态/vcpu vtimer ctx)。"""
    s = _FOREN_S
    if s is None:
        return
    w = b""

    def _recv(until, stop_on=b""):
        """收串口;stop_on 只认**本段新增**——累积缓冲里旧锚立即命
        中会让双 vmstat 判活空转(站28 取证修真的自挖坑)"""
        nonlocal w
        base = len(w)
        end = time.time() + until
        while time.time() < end:
            try:
                w += s.recv(4096)
            except socket.timeout:
                if stop_on and stop_on in w[base:]:
                    return
                continue
            except OSError:
                return
            if stop_on and stop_on in w[base:]:
                return
    try:
        s.settimeout(0.3)
        s.sendall(b"\x1d")
        _recv(3, b"console -> host")
    except Exception as e:
        print("forensics: escape stage failed: %r" % (e,))
    try:
        s.sendall(b"vmstat\n")
        _recv(4, b"preempts: ")
        s.sendall(b"vmstat\n")          # 站28:双读判宿主 tick 存活
        _recv(4, b"preempts: ")
        s.sendall(b"vmrx\n")
        _recv(3)
        s.sendall(b"vmtrail 512\n")
        _recv(15, b"\n")
    except Exception as e:
        print("forensics: serial stage failed: %r" % (e,))
    try:
        print("=== station-25 auto-forensics (gate failure) ===")
        print(w.decode(errors="replace")[-4000:])
    except Exception:
        pass
    try:
        _forensic_gdb()
    except Exception as e:
        print("forensics: gdb stage failed: %r" % (e,))


def _forensic_gdb():
    """站28 vtimer 专站:门红时挂 gdb(:1234,-s 起机判据零影响)。
    逐核读 CNTHP/CNTV/CNTPCT/CNTFRQ(四核齐死=共享面,单核死=per-cpu
    active/pending,一眼分流);宿主静态 freq/ticks 与 GICR 四核
    ISENABLER0/ISACTIVER0(eoi_drop 陈 active 位即现形);vcpu 帧
    vtimer ctx。宿主 VA 读取须 EL2 语境——thread apply all 全试,
    guest 语境核拒读属预期输出。"""
    cmds = [
        "set pagination off",
        "target remote :1234",
        "thread apply all x/i $pc",
        "thread apply all info registers cnthp_cval_el2 cnthp_ctl_el2"
        " cntv_cval_el0 cntv_ctl_el0 cntpct_el0 cntfrq_el0",
        "thread apply all x/2gx 0x40288000",
        "thread apply all x/1wx 0x08000000",
        "thread apply all x/1wx 0x080B0000+0x300",
        "thread apply all x/1wx 0x080D0000+0x300",
        "thread apply all x/1wx 0x080F0000+0x300",
        "thread apply all x/1wx 0x08110000+0x300",
        "thread apply all x/1wx 0x080B0000+0x200",
        "thread apply all x/1wx 0x080D0000+0x200",
        "thread apply all x/1wx 0x080F0000+0x200",
        "thread apply all x/1wx 0x08110000+0x200",
        "thread apply all x/8gx 0x40276100",
        "thread apply all x/8gx 0x4020f010",
        "detach",
    ]
    g = subprocess.run(
        ["gdb-multiarch", "-q", "-batch"] + sum([["-ex", c] for c in cmds], []),
        capture_output=True, text=True, timeout=180)
    print("=== station-28 gdb forensics (gate failure) ===")
    print(g.stdout[-7000:])
    print("--- gdb stderr tail ---")
    print(g.stderr[-2000:])


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
            "-s",                       # 站28:门红自动取证的 gdb 桩(无连接零开销)
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
        global _FOREN_S
        _FOREN_S = s                      # 站25:门红自动取证挂点

        buf = b""
        try:
            # ---- 宿主三连 ----
            buf = read_until(s, buf, ("eshine-os " + VER).encode(), time.time() + 30)
            print("  [1/81] banner ok (" + VER + ")")
            buf = read_until(s, buf, b"eshine> ", time.time() + 10)
            print("  [2/81] shell prompt ok")

            # ---- 站十八D/站二十:辅核起立(PSCI CPU_ON;上线打印在提示符
            #      前握手;-smp 4 后 cpu2/cpu3 同由宿主 PSCI 引导,[3] 扩三行)----
            for core in (b"cpu1", b"cpu2", b"cpu3"):
                if core + b": online (psci cpu_on)" not in buf:
                    raise AssertionError("%s online line missing: %r" % (core, buf[-300:]))
            print("  [3/81] cpu1/2/3 online (psci cpu_on) — three secondary cores up")
            prompt_pos = len(buf)
            token = "ESHINEOK%d" % os.getpid()
            buf = token_echo(s, buf, token)
            print("  [4/81] host token double-echo ok")

            # ---- A 相:真中断判据 ----
            buf, b4, a4 = tick_pair(s, buf, "host tick")
            print("  [5/81] tick grows: %d -> %d (CNTHP->GICv3->EL2 IRQ)" % (b4, a4))

            pos = len(buf)
            s.sendall(b"vmls\n")
            buf = read_until(s, buf, b"[0] VM_INIT", time.time() + 10, start=pos)
            buf = read_until(s, buf, b"[1] VM_INIT", time.time() + 10, start=pos)
            print("  [6/81] vmls: empty table")

            pos = len(buf)
            s.sendall(b"vmcreate 0\n")
            buf = read_until(s, buf, b"vm0: CREATED", time.time() + 10, start=pos)
            print("  [7/81] vmcreate 0 -> CREATED")

            # ---- vmconsole 状态门:非 RUNNING 不可前台化(默认拒绝)。
            # owner 指给不消费输入的 VM = 终端永久锁死(输入全入环无人
            # 读,vmconsole host 自救同样被路由)——实锤修复的回归判据。 ----
            pos = len(buf)
            s.sendall(b"vmconsole 0\n")
            buf = read_until(s, buf, b"denied: not running", time.time() + 10, start=pos)
            print("  [8/81] vmconsole state gate: denied before vmrun")

            # ---- VM0:异步启动(后台跑动,banner 经 vuart 陷阱整行前缀冲) ----
            # 站十六:VM0 槽 = eshine-rtos(任务内核客);banner 即 RTOS 身份
            pos = len(buf)
            s.sendall(b"vmrun 0\n")
            buf = read_until(s, buf, b"eshine> ", time.time() + 10, start=pos)
            buf = read_until(s, buf, ("[vm0] eshine-rtos " + VER).encode(),
                             time.time() + 20, start=pos)   # 后台 banner:[vmN] 前缀判据(第六站)
            print("  [9/81] vmrun 0 async -> rtos banner via vuart ([vm0] prefix)")

            # ---- DTB 自查(第十一站):三设备身份从注入树查出(非硬编码) ----
            buf = read_until(s, buf, b"[vm0] fdt ok: cpu arm,cortex-a57",
                             time.time() + 10, start=pos)
            print("  [10/81] fdt self-check: cpu node (arm,cortex-a57)")
            buf = read_until(s, buf, b"[vm0] fdt ok: uart arm,pl011",
                             time.time() + 10, start=pos)
            print("  [11/81] fdt self-check: uart node (arm,pl011)")
            buf = read_until(s, buf, b"[vm0] fdt ok: blk virtio,mmio",
                             time.time() + 10, start=pos)
            print("  [12/81] fdt self-check: blk node (virtio,mmio)")

            # ---- PL011 完整化(第十三站):AMBA 身份证+开机(CR 真语义) ----
            buf = read_until(s, buf, b"[vm0] pl011 on: amba 00141011",
                             time.time() + 10, start=pos)
            print("  [13/81] pl011 amba id 0x00141011 (PERIPHID/PCELLID probe)")

            # ---- vGIC 真启动式初始化(第十二站):distributor 成为注入权威 ----
            buf = read_until(s, buf, b"[vm0] gicd v3 ok", time.time() + 10,
                             start=pos)
            print("  [14/81] gicd v3 probe via emulated GICD (PIDR2 == 0x30)")
            buf = read_until(s, buf, b"[vm0] gic on: 27 33 35", time.time() + 10,
                             start=pos)
            print("  [15/81] gic enabled via GICD/GICR (ppi27 + spi33/35 readback)")

            # ---- UART IRQ 模式(第八站):guest 使能 RXIM 后自报 ----
            buf = read_until(s, buf, b"[vm0] uart irq on", time.time() + 10,
                             start=pos)
            print("  [16/81] guest enabled RXIM -> uart irq mode (vSPI 33 path)")

            # ---- virtio 就绪(第十站):probe 三验+队列+DRIVER_OK 自报 ----
            buf = read_until(s, buf, b"[vm0] virtio blk on", time.time() + 10,
                             start=pos)
            print("  [17/81] virtio probe + DRIVER_OK -> virtio blk on (vSPI 35 path)")

            # ---- 站十六:RTOS 世界起立(位图调度器+tick+任务) ----
            buf = read_until(s, buf, b"[vm0] rtos: up (eshine-rtos", time.time() + 10,
                             start=pos)
            print("  [18/81] rtos up: 32-prio bitmap scheduler, tick 100Hz")

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
            print("  [19/81] RX wake chain: push -> irq -> shell task READY -> prompt")

            p = len(buf)
            s.sendall(b"selftest\n")
            buf = read_until(s, buf, b"selftest OK", time.time() + 20, start=p)
            buf = read_until(s, buf, b"rtos> ", time.time() + 10, start=p)
            print("  [20/81] VM0 (rtos) selftest OK (x19/x30 live across 100 MMIO traps)")

            # ---- 站十六:ps——任务表活体(shell/heartbeat/idle 三行在场) ----
            p = len(buf)
            s.sendall(b"ps\n")
            buf = read_until(s, buf, b"rtos> ", time.time() + 10, start=p)
            for row in (b"shell", b"heartbeat", b"idle"):
                if row not in buf[p:]:
                    raise AssertionError("ps missing row %r: %r" % (row, buf[p:][-200:]))
            print("  [21/81] ps: shell/heartbeat/idle rows (task table live)")

            # ---- 站十六:spawn——动态任务生-问候-亡全链 ----
            p = len(buf)
            send_guest(s, b"spawn gate16\n")
            buf = read_until(s, buf, b"gate16: hello from task", time.time() + 15, start=p)
            buf = read_until(s, buf, b"rtos> ", time.time() + 10, start=p)
            print("  [22/81] spawn gate16 -> born, greeted, exited to pool")

            # ---- 站十六:fp 自证——1e6 double 和跨任务/跨客切换精确 ----
            p = len(buf)
            s.sendall(b"fp\n")
            buf = read_until(s, buf, b"rtos> ", time.time() + 10, start=p)   # 壳立回(任务异步)
            buf = read_until(s, buf, b"fp OK: 500000500000", time.time() + 60, start=p)
            print("  [23/81] fp self-proof: 1e6 double sum exact (SIMD frame across VM/task switches)")

            # ---- 站十六:壳任务睡 3s,心跳任务不睡——窗口内必有 alive 行 ----
            p = len(buf)
            s.sendall(b"sleep 3\n")
            buf = read_until(s, buf, b"sleeping...", time.time() + 10, start=p)
            buf = read_until(s, buf, b"slept (rtos sleep)", time.time() + 20, start=p)
            p_end = buf.index(b"slept (rtos sleep)", p)
            if b"rtos: alive" not in buf[p:p_end]:
                raise AssertionError("no heartbeat during shell sleep: %r" % buf[p:p_end][-200:])
            print("  [24/81] shell sleep 3s: heartbeat interleaved (task-level concurrency)")

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
            print("  [25/81] HVC#2 yield -> PAUSED -> vmrun resume -> '[vm0] resumed' (task ctx intact)")

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
            print("  [26/81] HVC#4 handback + vmstat readable (preempts=%s so far)"
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
            print("  [27/81] semdemo: bounded ring 8/8 (sem pair, blocking ping-pong)")

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
            print("  [28/81] ps mid-hold: waiter BLOCK on mutex, contender SLEEP (live state)")

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
            print("  [29/81] mutex: block-wake order + recursion + non-owner denial")

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
            print("  [30/81] pidemo: PI starvation reversal (low 9->6->9, high got lock)")

            # ---- 站十八C:fpdual——per-task FP 双并发自证 ----
            p = len(buf)
            s.sendall(b"fpdual\n")
            buf = read_until(s, buf, b"fpdual: B OK: 250000250000", time.time() + 30, start=p)
            if b"fpdual: A OK: 500000500000" not in buf[p:]:
                raise AssertionError("fpdual A missing: %r" % buf[p:][-300:])
            print("  [31/81] fpdual: dual FP tasks both exact (per-task SIMD frame)")

            # ---- 站十九B:事件组——AND 双拍不醒+双拍齐醒+OR 单拍即醒 ----
            p = len(buf)
            s.sendall(b"eventdemo\n")
            buf = read_until(s, buf, b"evt: waiter fired flags=0x5", time.time() + 30, start=p)
            buf = read_until(s, buf, b"evt: OR fired flags=0x8", time.time() + 15, start=p)
            a_ = buf.index(b"evt: waiter fired flags=0x5", p)
            if buf.index(b"evt: A set (waiter stays)", p) > a_:
                raise AssertionError("AND waiter woke before both bits: %r" % buf[p:][-300:])
            print("  [32/81] eventdemo: AND needs both bits, OR wakes on single")

            # ---- 站十九B:消息队列——16 条环 + 满发阻塞 ----
            p = len(buf)
            s.sendall(b"queuedemo\n")
            buf = read_until(s, buf, b"q: ring full, sender blocked", time.time() + 20, start=p)
            buf = read_until(s, buf, b"q: done 16/16", time.time() + 20, start=p)
            if buf[p:].count(b"q: sent ") < 16 or buf[p:].count(b"q: got ") < 16:
                raise AssertionError("queue 16-msg chain incomplete: %r" % buf[p:][-300:])
            print("  [33/81] queuedemo: 16 msgs ring + full-blocking handoff")

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
            print("  [34/81] fpstat: lazy FP %d saves << %d switches" % (n_sv, n_sw))

            # ---- 站二十:RTOS 次核自报(PSCI CPU_ON 经 hvc 泛化;GIC 帧 1
            #      使能回读 = MMIO 窗/nframes/使能门三层链全通的硬证) ----
            buf = read_until(s, buf, b"cpu1: online (rtos psci)", time.time() + 30)
            m = re.search(rb"cpu1: online \(rtos psci\) gicr1_en=(\d+)", buf)
            if not m or int(m.group(1)) != 0x08000001:
                raise AssertionError("secondary frame1 ISENABLER0 readback bad: %r"
                                     % buf[-300:])
            print("  [35/81] RTOS secondary online (rtos psci), frame1 en=0x08000001")

            # ---- 站二十:smpdemo——双核计数行交错(cpu1 行 >= 2) ----
            #      站23C 评审补:vm0 双帧行完整性 needle——次核 bg 行
            #      若被泊位冲刷拦腰撕,'cpu1: n=' 与数字间断开,连续型
            #      正则不中而子串仍在(子串数>正则数=撕裂,fire 复验
            #      的正向见证面)。 ----
            p = len(buf)
            s.sendall(b"smpdemo\n")
            buf = read_until(s, buf, b"cpu0: done=", time.time() + 20, start=p)
            buf = read_until(s, buf, b"cpu1: done=", time.time() + 20, start=p)
            c0 = buf[p:].count(b"cpu0: n=")
            c1 = buf[p:].count(b"cpu1: n=")
            m0 = len(re.findall(rb"cpu0: n=(\d+)", buf[p:]))
            m1 = len(re.findall(rb"cpu1: n=(\d+)", buf[p:]))
            if c1 < 2 or c0 < 2 or c1 != m1 or c0 != m0:
                raise AssertionError("smpdemo interleave/torn (cpu0 %d/%d, "
                                     "cpu1 %d/%d): %r"
                                     % (c0, m0, c1, m1, buf[p:][:400]))
            print("  [36/81] smpdemo: dual-core counters interleave, lines intact "
                  "(%d cpu1 lines)" % c1)

            # ---- 站二十:ipidemo——SGI 往返 >= 20 + 跨核唤醒 3 全链 ----
            #      站24A 评审补:demo 回合循环已移入独立任务 ipimain(原
            #      在壳语境睡,心跳按需化后 [37] 窗必夹 alive 行=用户令①
            #      点名要除的刷屏)——本窗断言零自发心跳,demo 期安静
            #      与提示符安静([47])同律成判据。 ----
            p = len(buf)
            s.sendall(b"ipidemo\n")
            buf = read_until(s, buf, b"ipi: rx0=", time.time() + 30, start=p)
            buf = read_until(s, buf, b"ipi: sleeper done", time.time() + 40, start=p)
            m = re.search(rb"ipi: rx0=(\d+) roundtrips", buf[p:])
            if not m or int(m.group(1)) < 20:
                raise AssertionError("ipi roundtrips < 20: %r" % buf[p:][:300])
            if buf[p:].count(b"ipi: cpu1 awake") != 3:
                raise AssertionError("sleeper wakes != 3: %r" % buf[p:][:400])
            if re.search(rb"rtos: alive", buf[p:]):
                raise AssertionError("spontaneous heartbeat inside ipidemo window "
                                     "(station-24A quiet-demo promise broken): %r"
                                     % buf[p:][:200:])
            print("  [37/81] ipidemo: %d SGI roundtrips + 3 cross-core wakes, window quiet"
                  % int(m.group(1)))

            # ---- 站二十:fpsmp——per-CPU FP 帧两核双精确 ----
            p = len(buf)
            s.sendall(b"fpsmp\n")
            buf = read_until(s, buf, b"fpsmp: cpu0 A OK: 500000500000", time.time() + 40,
                             start=p)
            buf = read_until(s, buf, b"fpsmp: cpu1 B OK: 250000250000", time.time() + 40,
                             start=p)
            print("  [38/81] fpsmp: per-CPU FP frames both exact")

            # ---- 站二十:次核在线时主核判据不回归(ps 表+fpstat 两核求和
            #      严格递增:二次读数含双核活动) ----
            p = len(buf)
            s.sendall(b"ps\n")
            buf = read_until(s, buf, b"rtos> ", time.time() + 15, start=p)
            if b"shell prio 5" not in buf[p:] or b"heartbeat" not in buf[p:]:
                raise AssertionError("ps regression with secondary online: %r"
                                     % buf[p:][-300:])
            p = len(buf)
            s.sendall(b"fpstat\n")
            buf = read_until(s, buf, b"fp-saves", time.time() + 15, start=p)
            m = re.search(rb"lazy: (\d+) switches, (\d+) fp-saves", buf[p:])
            if not m:
                raise AssertionError("fpstat 2nd reading format: %r" % buf[p:][-120:])
            n_sw2, n_sv2 = int(m.group(1)), int(m.group(2))
            if n_sw2 <= n_sw or n_sv2 > n_sw2:
                raise AssertionError("fpstat sum regression: %d vs %d (saves %d)"
                                     % (n_sw2, n_sw, n_sv2))
            print("  [39/81] no-regression with secondary online: ps ok, fpstat %d > %d"
                  % (n_sw2, n_sw))

            # ---- 站21A:tmodemo——超时面全弧(no-wait/满发超时/在时醒/
            #      no-wait 排空/空收超时 + PI 14->9->14 弧序断) ----
            p = len(buf)
            s.sendall(b"tmodemo\n")
            buf = read_until(s, buf, b"tmo: a done", time.time() + 45, start=p)
            w = buf[p:]
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
            print("  [40/81] tmodemo: timeout face full arc (no-wait x2, timeouts x4, "
                  "in-time wake, PI 14->9->14)")

            # ---- 站21C:stkdemo——受控深潜+水位+哨兵体检+ps 水位列 ----
            p = len(buf)
            s.sendall(b"stkdemo\n")
            buf = read_until(s, buf, b"stkdemo: dived", time.time() + 15, start=p)
            s.sendall(b"stackcheck\n")
            buf = read_until(s, buf, b"stk: magic ok", time.time() + 10, start=p)
            w = buf[p:]
            m = re.search(rb"stk: deepdiver margin (\d+)", w)
            m2 = re.search(rb"stk: shell margin (\d+)", w)
            if not m or not m2 or int(m.group(1)) >= int(m2.group(1)) \
                    or int(m.group(1)) > 1000:
                raise AssertionError("deepdiver margin judge: %r" % w[:300])
            p2 = len(buf)
            s.sendall(b"ps\n")
            buf = read_until(s, buf, b"ps: task table", time.time() + 10, start=p2)
            h = buf.index(b"ps: task table", p2) + len(b"ps: task table")
            buf = read_until(s, buf, b" stk ", time.time() + 5, start=h)
            if b" stk " not in buf[h:]:
                raise AssertionError("ps missing stk column: %r" % buf[h:][-200:])
            print("  [41/81] stkdemo: deepdiver margin %s << shell %s, magic ok, ps stk col"
                  % (m.group(1).decode(), m2.group(1).decode()))

            # ---- 站22B:swtdemo——软定时器三面(单发/周期自停/取消)。
            #      评审核三面硬化:单发计数==1(发即注销回归=每 tick 复发
            #      仍绿)、周期恰 3(重臂回归=错拍/过发仍绿)、done 后
            #      1.5s 尾读扩缺席窗(重登记/复活类回归在 done 后才现形) ----
            p = len(buf)
            s.sendall(b"swtdemo\n")
            buf = read_until(s, buf, b"swt: done", time.time() + 25, start=p)
            end = time.time() + 1.5
            while time.time() < end:
                try:
                    buf += s.recv(4096)
                except socket.timeout:
                    pass
            w = buf[p:]
            for needle in (b"swt: periodic 1", b"swt: periodic 2",
                           b"swt: periodic 3", b"swt: periodic stopped",
                           b"swt: oneshot fired", b"swt: cancelled",
                           b"swt: done"):
                if needle not in w:
                    raise AssertionError("swtdemo needle missing: %r" % needle)
            if w.count(b"swt: oneshot fired") != 1:
                raise AssertionError("oneshot fired %dx (deregister regression): %r"
                                     % (w.count(b"swt: oneshot fired"), w[:300]))
            if len(re.findall(rb"swt: periodic \d", w)) != 3:
                raise AssertionError("periodic fired %dx (re-arm regression): %r"
                                     % (len(re.findall(rb"swt: periodic \d", w)),
                                        w[:400]))
            if b"swt: late" in w:
                raise AssertionError("cancelled timer fired: %r" % w[-200:])
            if not (w.find(b"swt: periodic 1") < w.find(b"swt: cancelled")
                    < w.find(b"swt: oneshot fired")
                    < w.find(b"swt: periodic 3")
                    < w.find(b"swt: periodic stopped")):
                raise AssertionError("swtdemo order broken: %r" % w[:300])
            print("  [42/81] swtdemo: oneshot x1 + periodic x3 self-stop + cancel "
                  "(late absent, counts exact)")

            # ---- 站23A:irqdemo——注册表活证(链式包装/计数/恢复) ----
            p = len(buf)
            s.sendall(b"irqdemo\n")
            time.sleep(0.1)         # 换钩先于 x 到达:同拍爆发会整批
                                    # 被换钩前的 ISR 消费(探针二轮
                                    # 定谳);0.1s=壳已处理完命令。demo
                                    # 自同步等探针(5 tick 一探预算
                                    # 2s),窗不必掐
            s.sendall(b"x\n")
            buf = read_until(s, buf, b"irqdemo: done", time.time() + 15, start=p)
            w = buf[p:]
            m = re.search(rb"irq: chained (\d+)", w)
            if not m or int(m.group(1)) < 1:
                raise AssertionError("irq chain count < 1: %r" % w[:300])
            p2 = len(buf)
            s.sendall(b"ps\n")      # 恢复后壳活:原 drain+RX 唤醒语义无损
            buf = read_until(s, buf, b"ps: task table", time.time() + 10, start=p2)
            print("  [43/81] irqdemo: registry chain x%s, uart slot restored, shell alive"
                  % m.group(1).decode())

            # ---- 站23B:lowdemo——贴线守卫(恰一次 warn,magic 不误伤) ----
            p = len(buf)
            s.sendall(b"lowdemo\n")
            buf = read_until(s, buf, b"lowdemo: done", time.time() + 20, start=p)
            w = buf[p:]
            if w.count(b"stk: lowmargin lowrunner") != 1:
                raise AssertionError("lowmargin warn count != 1: %r" % w[:300])
            p2 = len(buf)
            s.sendall(b"stackcheck\n")
            buf = read_until(s, buf, b"stk: magic ok", time.time() + 10, start=p2)
            print("  [44/81] lowdemo: guard warned once at depth, magic untouched")

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
            print("  [45/81] vmrun 1 async -> bare-metal banner via vuart ([vm1] prefix)")
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
            print("  [46/81] both RUNNING (preempts=%s so far)" % m.group(1).decode())

            # ---- 双客交错活:RTOS 壳 token(HVC 路由) ----
            pos = len(buf)
            s.sendall(b"vmconsole 0\n")
            buf = read_until(s, buf, b"console -> vm", time.time() + 10, start=pos)
            p2 = len(buf)
            s.sendall(b"\n")   # 空行:触发壳任务重打 prompt
            buf = read_until(s, buf, b"rtos> ", time.time() + 10, start=p2)
            buf = token_echo(s, buf, "GOKA%d" % os.getpid())
            # 站24A:提示符安静承诺成判据——壳全程 BLOCK/RUN,无
            # RT_SLEEP 态,窗口内零自发心跳(用户令:进去不刷屏)。
            if re.search(rb"rtos: alive", buf[p2:]):
                raise AssertionError("spontaneous heartbeat at interactive prompt "
                                     "(station-24 quiet-shell promise broken): %r"
                                     % buf[p2:][-200:])
            pos2 = len(buf)
            s.sendall(b"host\n")           # 前台交还 RX
            buf = read_until(s, buf, b"console -> host", time.time() + 10, start=pos2)
            print("  [47/81] vm0 (rtos) echo double-echo ok (foreground)")

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
            print("  [48/81] virtio write/read + completion IRQ (3-desc chain -> vSPI 35)")

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
            print("  [49/81] vm1 echo double-echo ok (both alive, interleaved)")

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
            print("  [50/81] spin 3G survived %d quantum preemptions (frame migration intact)"
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
            print("  [51/81] vm1 WAIT -> host alive, rtos running (fairness)")

            buf = read_until(s, buf, b"[vm1] slept (vtimer irq)", time.time() + 20)
            print("  [52/81] vm1 woke in background rotation (WOKE->pump inject)")

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
            print("  [53/81] dual sleep: rtos tick-sleep (2s) beats bare-metal one-shot (4s); per-VM vtimer ctx")

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
            print("  [54/81] both shells alive after dual sleep (rtos echo ok)")

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
            print("  [55/81] virtio persistence: data survives destroy+recreate")

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
            print("  [56/81] vm1 fault isolated: VM_DEAD, host alive")

            pos = len(buf)
            s.sendall(b"vmls\n")
            buf = read_until(s, buf, b"[0] VM_RUNNING", time.time() + 10, start=pos)
            buf = read_until(s, buf, b"[1] VM_DEAD", time.time() + 10, start=pos)
            buf = read_until(s, buf, b"eshine> ", time.time() + 10, start=pos)
            print("  [57/81] vmls: rtos RUNNING (kept for concurrency judge), bare-metal DEAD")

            prompt_pos = len(buf)
            token2 = "ESHINEOK%d" % (os.getpid() + 1)
            buf = token_echo(s, buf, token2)
            print("  [58/81] host shell intact after bare-metal lifecycle")

            # ---- guest 退场后 tick 仍增长(HCR/DAIF 恢复) ----
            buf, b20, a20 = tick_pair(s, buf, "post-guest tick")
            print("  [59/81] post-guest tick grows: %d -> %d (HCR_EL2_HOST/DAIF restore)"
                  % (b20, a20))

            pos = len(buf)
            s.sendall(b"vmkill 1\n")
            buf = read_until(s, buf, b"kill denied", time.time() + 10, start=pos)
            print("  [60/81] vmkill deny path ok (default-deny)")

            pos = len(buf)
            s.sendall(b"vmstat\n")
            buf = read_until(s, buf, b"[1] VM_DEAD", time.time() + 10, start=pos)
            print("  [61/81] vmstat still sane after teardown")

            # ---- 第十四站:Linux bring-up(槽 2,Image 外置 GPL 隔离)----
            pos = len(buf)
            s.sendall(b"vmcreate 2\n")
            buf = read_until(s, buf, b"vm2: CREATED", time.time() + 15, start=pos)
            p2 = len(buf)
            s.sendall(b"vmrun 2\n")
            buf = read_until(s, buf, b"Booting Linux on physical CPU", time.time() + 90, start=p2)
            p_boot = buf.index(b"Booting Linux on physical CPU", p2)   # 站十六:交错判据锚
            print("  [62/81] Linux enters via stage-2 + DTB x0 (arm64 boot protocol)")

            # ---- 站十九D:guest SMP——第二 vCPU 上线(Linux 引导真行;
            #      设计稿所引 "Bringing up 1 secondary CPU(s)" 实测不存在,
            #      以真行为准,spec §8 纠偏)----
            pos = len(buf)
            buf = read_until(s, buf, b"smp: Brought up 1 node, 2 CPUs", time.time() + 120)
            if b"CPU1: Booted secondary processor" not in buf[pos:]:
                raise AssertionError("secondary vCPU never booted (SMP broken): %r"
                                     % buf[pos:][-300:])
            print("  [63/81] guest SMP: secondary vCPU up (Brought up 1 node, 2 CPUs)")

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
            print("  [64/81] true parallelism: %d vm2 boot lines advanced inside vm1 spin (2nd physical CPU)"
                  % len(ts))
            # ---- 站23C:echo 风暴——延迟冲刷判据([54] 残余收编)。
            #      真并行窗内(vm2 靴流正冲)vm1 前台壳 10 发唯一
            #      token,判 10/10 连续;碎片期(泊位即时冲)实测
            #      23/60 撕,延迟化后 0 撕。评审 blocker:旧落点在
            #      [50] 后 owner=host 且 vm2 未建——needle 由宿主壳
            #      自产,门恒绿空转;迁窗至此定谳。 ----
            p = len(buf)
            for i in range(10):
                s.sendall(("echo storm%03d\n" % i).encode())
                time.sleep(0.3)
            end = time.time() + 2
            while time.time() < end:
                try:
                    buf += s.recv(4096)
                except socket.timeout:
                    pass
            w = buf[p:]
            intact = sum(1 for i in range(10)
                         if ("storm%03d" % i).encode() in w)
            if intact != 10:
                raise AssertionError("echo storm %d/10 intact (deferred flush): %r"
                                     % (intact, w[:400]))
            print("  [65/81] echo storm 10/10 intact through vm2 boot spam "
                  "(deferred park flush)")

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
            print("  [66/81] per-VM honest DTB: memory/cpus/timer nodes accepted")

            pos = len(buf)
            buf = read_until(s, buf, b"arch_timer: cp15 timer(s) running at 62.50MHz (virt)",
                             time.time() + 30)
            buf = read_until(s, buf, b"clocksource: arch_sys_counter", time.time() + 30)
            print("  [67/81] vtimer wiring (interrupt-names virt) + tick timekeeping")

            pos = len(buf)
            buf = read_until(s, buf, b"GICv3: CPU0: found redistributor 0",
                             time.time() + 30)
            print("  [68/81] Linux gic-v3 on emulated GICD/GICR (spec word decode)")

            # ---- 站十六:双客并发时序证明。窗口取"解掩蔽引导段":Linux
            #      早期 PSTATE.I=1,量子无从耗尽,泵单客模式直跑(活客短暂
            #      饿死系 guest 掩蔽所致,非宿主失职);local_irq_enable 后
            #      泵恢复轮转。站24A 起周期心跳只在壳自主睡时说话(提示
            #      符=RT_BLOCK 静默),本窗改 hb 注入:vm0 前台壳收到命令
            #      即回 alive 行,tick 严格递增=vm0 在 vm2 靴流窗内真实
            #      行进(双客并发),且 hb 输入路径本身即 [41] 输入不变量
            #      的见证面(泊位→推环→醒→回,一窗全验)。 ----
            p_v3 = buf.rindex(b"GICv3: CPU0: found redistributor 0")
            end = time.time() + 5
            next_hb = time.time() + 0.5
            while time.time() < end:
                if time.time() >= next_hb:
                    s.sendall(b"hb\n")          # 注入三发(0.5/2.5/4.5s)
                    next_hb += 2.0
                s.settimeout(0.5)
                try:
                    chunk = s.recv(4096)
                    if not chunk:
                        raise ConnectionError("serial closed during interleave window")
                    buf += chunk
                except socket.timeout:
                    pass
            w_hb = buf[p_v3:]
            hb_ticks = [int(x) for x in re.findall(rb"rtos: alive tick=(\d+)", w_hb)]
            if len(hb_ticks) < 2:
                raise AssertionError("hb replies < 2 inside Linux unmasked boot stream "
                                     "(input invariant broken — replies are the station-24B face): %r"
                                     % w_hb[-300:])
            if hb_ticks != sorted(hb_ticks) or len(set(hb_ticks)) != len(hb_ticks):
                raise AssertionError("hb tick values not strictly increasing (replies "
                                     "arrived = input invariant intact; frozen ticks = "
                                     "the residual vtimer-stall face, route to the "
                                     "wake-architecture station): %r" % hb_ticks)
            print("  [69/81] dual-guest concurrency: %d hb replies, ticks %s..%s "
                  "advance inside Linux boot" % (len(hb_ticks), hb_ticks[0], hb_ticks[-1]))

            pos = len(buf)
            buf = read_until(s, buf, b"printk: console [ttyAMA0] enabled", time.time() + 60)
            print("  [70/81] PL011 full driver + console handover (AMBA probe)")

            pos = len(buf)
            buf = read_until(s, buf, b"virtio_blk virtio0: [vda] 128 512-byte logical blocks",
                             time.time() + 90)
            print("  [71/81] virtio-mmio legacy + vblk under Linux (completion IRQ chain)")

            # ---- 第十五站:initramfs 用户空间(busybox PID 1 shell)----
            pos = len(buf)
            buf = read_until(s, buf, b"Freeing unused kernel memory", time.time() + 60)
            buf = read_until(s, buf, b"Run /init as init process", time.time() + 30)
            print("  [72/81] initramfs chain: DTB initrd props -> unpack -> /init takes over")

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
            print("  [73/81] busybox shell prompt (PID 1 userland via vmconsole)")

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
            print("  [74/81] shell token double echo (tty echo + sh output)")

            pos = len(buf)
            send_guest(s, b"cat /proc/cpuinfo\n")
            buf = read_until(s, buf, b"~ # ", time.time() + 30, start=pos)
            if b"CPU part\t: 0xd07" not in buf[pos:]:
                print("FAIL: /proc/cpuinfo missing cortex-a57 self-proof")
                print("raw tail: %r" % buf[pos:][-1200:])
                return 1
            print("  [75/81] userland self-proof: cat /proc/cpuinfo -> CPU part 0xd07")

            # ---- 站十九D:双核用户态自证——guest 内第二处理器可见 ----
            #      站24B:超时 30s→10s 收紧——本门形状=大 dump 完成后
            #      泊位+立即输入,正是 [41]/[76] 输入蒸发脸的复现器;
            #      健康轮 ~1s(探针实测),旧脸 30s+,10s 上界=输入
            #      不变量的诚实见证(判据只增不减的真强化)。 ----
            pos = len(buf)
            send_guest(s, b"cat /proc/cpuinfo\n")
            buf = read_until(s, buf, b"processor\t: 1", time.time() + 10, start=pos)
            buf = read_until(s, buf, b"~ # ", time.time() + 15, start=pos)
            print("  [76/81] userland SMP self-proof: cpuinfo -> processor 1 (2nd vCPU)"
                  " [input-after-park <=10s: station-24B invariant]")

            # ---- 站28:utest——EL0 syscall 面一门全验(Linux arm64 ABI:
            #      CurrentEL=0 自证/getpid>0/未注册号 -ENOSYS/sysinfo
            #      ver+tick 回填)。插在迟退前=RTOS 会话最后一门。
            #      接线律:此刻 owner=vm2 busybox([76] 收尾),宿主命令
            #      字节会被喂进 Linux——切台必走热键 \x1e0(与下门
            #      [78] 同律),首版 vmconsole 0 直发被 busybox 吞,
            #      "console -> vm" 永不至(timeout 定谳)。 ----
            pos = len(buf)
            s.sendall(b"\x1e0")
            buf = read_until(s, buf, b"console -> vm", time.time() + 10, start=pos)
            p2 = len(buf)
            s.sendall(b"\n")
            buf = read_until(s, buf, b"rtos> ", time.time() + 10, start=p2)
            p3 = len(buf)
            s.sendall(b"utest\n")
            buf = read_until(s, buf, b"utest: pid=", time.time() + 10, start=p3)
            buf = read_until(s, buf, b"EL0 syscall OK", time.time() + 10, start=p3)
            print("  [77/81] utest: EL0 syscall face (getpid/ENOSYS/sysinfo;SVC channel itself = EL0 proof)")

            # ---- 站26:控制台快捷键——Ctrl-^(0x1e) 前缀直切。此窗三台
            #      全在场(vm0/vm1/vm2 RUNNING,owner=vm2 busybox),
            #      一门全验:\x1e0 切 vm0 后 hb 回复(热键切换后输入
            #      链完整:switch->push->醒->回,同站 24 不变量)、
            #      \x1e1/\x1e2 存在性、\x1eh 回宿主(逃生 0x1d 契约
            #      不动,后续旧门流程从宿主面继续)。 ----
            pos = len(buf)
            s.sendall(b"\x1e0")
            buf = read_until(s, buf, b"console -> vm", time.time() + 10, start=pos)
            p2 = len(buf)
            s.sendall(b"hb\n")
            buf = read_until(s, buf, b"rtos: alive", time.time() + 10, start=p2)
            pos = len(buf)
            s.sendall(b"\x1e1")
            buf = read_until(s, buf, b"console -> vm", time.time() + 10, start=pos)
            pos = len(buf)
            s.sendall(b"\x1e2")
            buf = read_until(s, buf, b"console -> vm", time.time() + 10, start=pos)
            pos = len(buf)
            s.sendall(b"\x1eh")
            buf = read_until(s, buf, b"console -> host", time.time() + 10, start=pos)
            print("  [78/81] console hotkeys: ctrl-^ 0/1/2 switch + h host (hb alive via hotkey)")

            # ---- 站十六:RTOS 迟退(留活跨过双客并发判据后收场) ----
            # 站26:控制台已被新门 [77] 的 ctrl-^h 收归宿主(逃生 0x1d
            # 路径的覆盖由 [73] 门保留——彼处同为 escape 锚)
            pos = len(buf)
            s.sendall(b"vmconsole 0\n")
            buf = read_until(s, buf, b"console -> vm", time.time() + 10, start=pos)
            p2 = len(buf)
            s.sendall(b"\n")
            buf = read_until(s, buf, b"rtos> ", time.time() + 10, start=p2)
            p = len(buf)
            s.sendall(b"exit\n")
            buf = read_until(s, buf, b"vm0: VM_DEAD", time.time() + 15, start=p)
            print("  [79/81] rtos exit -> VM_DEAD (HVC#1 face, RX auto-reclaimed)")

            # ---- 站21B+C:overflow——受控越哨→tick 扫描→rt_panic 指名。
            #      vm0 已死于 [73](exit 判据),DEAD 槽直接重建(不可先
            #      vmkill:DEAD 态会被拒绝路径 deny——评审核);vm2-Linux
            #      仍在场,多 VM 并存语境下的真实运维复活路径 ----
            pos = len(buf)
            s.sendall(b"vmcreate 0\n")
            buf = read_until(s, buf, b"vm0: CREATED", time.time() + 15, start=pos)
            pos = len(buf)
            s.sendall(b"vmrun 0\n")
            buf = read_until(s, buf, b"rtos: up", time.time() + 20, start=pos)
            buf = read_until(s, buf, b"cpu1: online (rtos psci)", time.time() + 30,
                             start=pos)
            pos = len(buf)
            s.sendall(b"vmconsole 0\n")
            buf = read_until(s, buf, b"console -> vm", time.time() + 10, start=pos)
            pos = len(buf)
            s.sendall(b"\n")
            buf = read_until(s, buf, b"rtos> ", time.time() + 10, start=pos)
            pos = len(buf)
            s.sendall(b"overflow\n")
            buf = read_until(s, buf, b"rtos panic: stack overflow", time.time() + 20,
                             start=pos)
            buf = read_until(s, buf, b"context: tick scan", time.time() + 5, start=pos)
            if b"task: overflower" not in buf[pos:]:
                raise AssertionError("panic did not name overflower: %r"
                                     % buf[pos:][-200:])
            pos = len(buf)
            s.sendall(b"\x1d")           # panic 壳即死:逃生夺回再处决
            buf = read_until(s, buf, b"console -> host (escape)", time.time() + 10,
                             start=pos)
            pos = len(buf)
            s.sendall(b"vmkill 0\n")
            buf = read_until(s, buf, b"killed (host authority)", time.time() + 15,
                             start=pos)
            print("  [80/81] overflow -> panic names overflower (tick-scan face); vm0 reaped")

            pos = len(buf)
            s.sendall(b"vmkill 2\n")
            buf = read_until(s, buf, b"killed (host authority)", time.time() + 15, start=pos)
            buf, b21, a21 = tick_pair(s, buf, "post-linux tick")
            if a21 <= b21:
                print("FAIL: host tick frozen after Linux lifecycle")
                return 1
            token3 = "ESHINEOK%d" % (os.getpid() + 3)
            buf = token_echo(s, buf, token3)
            print("  [81/81] shell VM killed (authority); host intact (tick %d->%d, token echo)"
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
    try:
        sys.exit(main())
    except SystemExit:
        raise
    except BaseException:
        _forensic_dump()                  # 站25:门红先取证再抛
        raise
