# 站五十:slog 持久化+klog 双源+logctrl+procfs 扩面

日期:2026-11-02 | 批次 A 第二站
参照:参考实现 slog(log_file.c 持久化/10MB×10 轮转/sys-ker 双前缀/水位+
周期 flush/log_ctrl 控制工具)与 /proc(28 文件,seq_file 行游标)机制
白描;ramfs COW 写路径(站 39)前提已备。

## §1 v0 形状

- **持久化**:slogsrv(内核任务)以 resmgr 客户端身份走消息面写
  /var/log(服务器互为客户端=微内核正道,零内部 API 泄漏):
  open/create(经 /dev/ramfs 元名)+SEEK END+WRITE 追加(44B 窗分块)
  +STAT 尺寸影子;8MB 页池下 10MB×10 轮转不可为——**双世代交替命名
  轮转**(slog/slog.1 互为前世今生,rotate=unlink 对侧+create 对侧,
  零拷贝),3.5KB 触发。
- **klog 双源**:内核 elog 环(站 49 32KiB)收割入文件——核心新增
  `eshine_log_ring_delta(cursor)` 增量收割 API(弱锁对 log_ring_lock/
  unlock,后端 override 与存环同锁,环读一致);slogsrv recv 改 1s
  超时醒=周期 flush(参考实现周期 flush 10s 的站内形);用户 slog WRITE
  面追加 'S' 前缀行(双源 S=system/K=kernel 双前缀面)。
- **logctrl**:`logctrl` 命令(demo 蹦床):flush(立即收割)/stat
  (双世代尺寸+环游标)/echo on|off(warn+ 控制台回显开关)。
- **procfs 扩面**:OPEN 按路径分型(参考实现 seq_file 行游标站内形):
  ""=任务表(既有)/meminfo(页池 2048 页 total/used/free)/cpuinfo
  (双核行)/interrupts(irq 表计数行,dispatch 逐中断 u64 计数新域)。

## §2 判据(门禁 139→145,append-only)

- [140] vm0 slogdemo+klog flush:logctrl flush -> 收割 N>0
- [141] /var/log/slog 读回(busybox cat):内核行+用户行双源同卷
- [142] 轮转:灌满(循环写)-> stat 双世代(slog+slog.1 皆在役)
- [143] cat /proc/meminfo -> MemTotal/MemFree 行
- [144] cat /proc/cpuinfo -> processor 行 ×2
- [145] cat /proc/interrupts -> 27/33/35 计数行(计数>0)

## §3 非目标(记档)

- 压缩落盘(zlib=站 59 消费者)/时间命名轮转(ramfs 无时钟排序
  价值)/水位 flush(1s 周期已覆盖)/logctrl -f follow(无第二终端)
- /proc 全 28 文件面(fd/loadavg/net…按消费者上门)
