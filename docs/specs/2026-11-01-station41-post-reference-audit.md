# 站 41 后参考实现差距终审:四路机制普查与补齐账册

日期:2026-11-01 · 作者:Julian Chu + Claude Code
方法:clean-room 机制白描四路代理普查(core+arch / syscall+include /
drivers+bsp / lib+test,522 文件),对照我们 30 文件 RTOS;本文=四报告
合成+树内判读(审计的"不确定"项全部由本人对着我们的仓核实)。

## 0. 判读基准

参考实现=Linux 机制内嵌+QNX 消息生态+车规取证三源混合体;我们=真
Linux 号面+resmgr+最小诚实站内形。站 27 差距清单(线程→shm→
mqueue→信号完整→FS→服务生态→CAPR)已由站 28-41 兑现。本审计
目标:抓站 27 清单**漏掉的**。

## 1. 真遗漏(v0 内该有,补齐=站 42 候选面)

**A. 调度器管理 syscall 组(整组缺)**——sched_getparam/setparam/
get/set_scheduler(策略)/get_priority_max/min/rr_get_interval/
setaffinity/getaffinity/getcpu/yield 号面/nice。真 Linux 号(实现
期从 inst 头钉号)。体量中。
**B. 信号面补全**——sigpending/sigsuspend/sigaltstack/
sigtimedwait(sigtimedwait 供 musl pthread 取消点)。我们只有
kill/action/return/procmask/queue。体量小-中。
**C. posix timer+itimer 完整面**——timer_create/delete/settime/
gettime/getoverrun(sigevent 通知)+getitimer/setitimer。我们
timer=站 20 oneshot/periodic 内核面,无 POSIX sigevent 型。
体量中。
**D. robust futex list**——set/get_robust_list(真号 99/100),
musl pthread 启动即注册;持锁者死亡自动释锁。我们 futex 只有
WAIT/WAKE。体量中。
**E. ERESTARTSYS 可重启系统调用语义**——阻塞调用被打断返
512-516 内部码,出口统一决定重启;配套 sem/take 的 interruptible/
killable 变体。我们 futex 有 -EINTR 面,无重启语义。体量中。
**F. 进程组/会话**——setsid/getsid/getpgid/setpgid(busybox
作业控制面)。体量小-中。
**G. close_on_exec(fd 表 FD_CLOEXEC 位)**——fcntl F_SETFD
我们面薄。体量小。
**H. 观测诊断面**——calltrace(FP 链回溯,panic 自动触发)/富
任务画像(sp/pc/阻塞对象归属查询,死锁排查)/sysinfo 扩展。
我们 ps 薄。体量中。
**I. poll/select 面**——fd 上的多路等待(busybox select 用户)。
体量中。
**J. 杂项真号补齐**——uname(160)? getrusage? waitid? 按需
(busybox/musl 实测召唤,不臆测——qemu-user -strace 纪律)。

## 2. 站 42 提案

**站 42=syscall 面补全+观测面**(真遗漏 A-I):真号面纪律+门禁
(116→~122)+musl/busybox 实测召唤定谳哪些号真正上门(不照单
全收参考实现的 230 个——我们按消费者上门讨,站 33 纪律)。
calltrace+阻塞对象查询=观测面旗舰。

## 3. 已记档偏差确认(不重复立项,后续弧)

- xspace 全局窗(我们=shm 段替代,站 36)/CFS(仅背景类)/
  APS 分区调度/MPRT 内存分区(车规 face,终态线候选站)/
  物理内存工程族(memblock/buddy/SLUB/CMA/percpu/struct-page
  ——我们 8MB 平铺页池 v0)/elf 动态链接(静态 musl v0)/
  hrtimer 完整栈(我们 vtimer 截留 v0)/ptrace+GDB/coredump+
  kerr 取证族/vDSO 数据页/零拷贝消息窗(我们 msgseg 单拷贝
  1KB 窗)/PM 通知消息化(我们 wait4 同步 v0)/VMM 操作消息
  化(架构路线差异)/用户态中断接管(v0 设备面全 EL1)。
- ITS/LPI/MSI:我们宿主 vgic 无 ITS 虚拟化,VM 内无硬件面,
  不适用;宿主加面后另立。
- USB/display/SoC 服务器群:QEMU virt 无硬件面,不适用。

## 4. 架构级形态差(记录,非缺项)

- 参考实现=内核只留 irqchip+earlycon,设备全推用户态服务器;我们
  =设备面 EL1+resmgr 文件面。两条路线,spec §偏差已记。
- 参考实现号面=私有 自有编号(0-305,230 stub)+libc 适配层;我们=
  真号面+musl 直连。两条路线,站 27/33 已定谳。
- 参考实现 VM0 兼做其他 VM 的设备模型提供者(virtio_dev_server);
  我们=纯设备消费者。终态(参考实现多 VM)会需要跨 VM 设备后端
  面——挂终态规划线。
- 参考实现服务治理面(driver_manager JSON 配置+就绪等待+waitpid
  监控恢复)+ham 健康面:我们的开机链=固定序列 C,治理面后弧。

## 5. 参考实现弱于我们的面(诚实账)

- getrandom=返回 0 的存根(我们站 40 两脸同源)/CAPR_COPY=no-op
  (我们 caps 按位继承真落码)/rlimit 号位保留未接线(我们
  rlimit_self 583 真执法)/rand/sigaltstack 等存根多;test 面
  CMakeLists 引用已不存在目录=测试面有失修;无 fuzz。

## 6. DoD

本账册→站 42 spec(面裁剪:A-I 按实测召唤)→实现→门禁×2→
三处同步→内容包→commit→镜像机→记忆。
