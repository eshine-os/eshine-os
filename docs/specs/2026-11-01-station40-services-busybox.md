# 站 40 spec:服务收编+busybox——生态面

日期:2026-11-01 · 作者:Julian Chu + Claude Code
参考实现机制白描(零拷贝红线):服务生态=userspace-server 统一层
(procfs/random/sock/time/drvfw/slogd/coredump);busybox init+inittab
形态=用户态 PID1 消费 rootfs。我们世界:v0 服务器仍是内核任务
(站 30-39 同形,userspace-server 迁移=后弧),inittab 由壳代
处理器承接(偏差记档)。

## 1. 我们世界里的四件事

现状盘点:vm0 用户态=musl hello/pt(站 33/35),ramfs 只读→
COW 可写(站 39),服务器 4 个(tty/ramfs/pipe/blk),无时间/
随机/proc/日志面。vm2 右翼=glibc busybox(站 15)。

### A. 服务收编三新服务器 + 时间真号
- **random**(第五服务器,/dev/random):fd READ 面=字节窗;
  熵源=cntvct 抖动+调度噪声混入 xorshift64*(非密码级 v0,
  记档);getrandom(278) 内核面同一混出器。
- **procfs**(第六服务器,/proc):OPEN=目录把手;READ=任务
  表文本行(tid name prio state,ocb.off=行游标);READDIR=活
  任务名枚举(站 39 游标协议第二消费者,非 ramfs 一家)。
- **slogd**(第七服务器,/dev/slog):fd WRITE=入环(首字节
  级别),READ=环倾倒;≥warn 控制台回显。环 16×48B。
- **时间真号**(内核面):clock_gettime(113)/clock_getres(114)
  /gettimeofday(169)——monotonic=cntvct/freq;realtime=
  BOOT_EPOCH(2026-11-01T00:00:00Z 常量,RTC/drvfw=后弧)+单调。
- **fd 面薄号**:openat(56)=fdopen 面(AT_FDCWD 语义);
  close(57)=fdclose;getdents64(61)=fd 层格式化 dirent64
  (d_type 给真:DT_REG/DT_DIR——ls 免 stat 族),每调一名,
  next-cursor 走 fd 私域 dircur。

### B. busybox:真 musl-static busybox 跑 vm0
- tools/build-busybox.sh:busybox 1.36.1(仓外已有 tar 源)×
  我们 musl 1.2.5 工具链(/tmp/musl-build/inst,零 patch 目标),
  静态,链接窗基 0x40C00000(站 33 同律:头入段 AT_PHDR)。
  **GPL-2.0 隔离照 vm2 先例:配方入仓,产物仓外**
  (tools/ramfs/bin/busybox 入 .gitignore)。
- loader 泛化:`run <path> [args...]`——argc/argv 任意参
  (启动块变长:串区自顶向下+argv 指针区+auxv 不变 10 项);
  UX_STACK_HINT 0x40D00000→0x40D80000(busybox ~1MB 图像+
  栈,窗内 2MB 账)。
- 读途成本:1MB/64B 窗≈16K 消息往返——fd READ 窗暂不动,
  冒烟实测为准(慢=后弧调窗宽,契约面不动)。
- 判据=**busybox 双世界**:同一 musl busybox 字节跑两个 OS
  (vm0 我们内核+loader;vm2 真内核)——md5sum 自证同哈希。
  build-initramfs.sh 改为优先落我们 musl busybox。

### C. init+inittab 形态(壳代处理器)
- /etc/inittab 四 action:sysinit/wait(跑完再继续)/once
  (不等)/respawn(退场即再拉)。wait4 补 WNOHANG options 位
  (POSIX 面,respawn 探询)。
- 壳启动序:处理 inittab(读文件走 fd 面消息,ramfs 服务器
  自调度起立——消息面自同步)→ 首提示符;每命令回毕轮询
  respawner(退即拉,v0 检查点=壳循环拍)。
- 演示件:/bin/initmark(musl 小件:fdcreate /tmp/init40 +
  写标记=sysinit 活证)/bin/beat(打印即退 exit=42=respawn
  活证);源入仓 tools/ramfs-{initmark,beat}.c,产物入仓
  (hello/pt 先例,我们代码 MIT)。

### D. 容量账(站 39 律:每加占用者重合)
- 任务池 16→20(常驻 8+3 服务器=11,run 子+respawn 子+demo
  峰 8-9)。
- 通道 8→16(开机 4+3 服务器=7,demo 瞬时余量)。
- 连接 16→24(壳 3+子 3×2+loader fd+demo 瞬时)。
- 名字 16→32(boot 8 名+busybox/inittab/beat/initmark/proc/
  random/slog=+7,demo 瞬时余量)。
- 进程 4 不动(壳+busybox 子+beat 子=3,账记档)。

## 2. 判据(103→111)

- [99] rngdemo:/dev/random 16B×2 读互异+getrandom 直面。
- [100] clockdemo:monotonic 递增+realtime 打点+res 1ns。
- [101] procdemo:/proc 枚举≥8+文本含 "shell"。
- [102] slogdemo:三级入环+倾倒 3 行(回显 warn+err)。
- [103] busybox echo "ESHINE40-BB"(fork→loader→musl busybox
  →writev 全链)。
- [104] busybox cat /etc/motd+date -u(时间面真消费)。
- [105] busybox ls /etc(getdents64 真面,motd 在列)。
- [106] init:inittab sysinit 落 /tmp/init40(busybox cat 出
  标记)+respawn beat≥2。
- [107] 双世界:vm0 md5sum /bin/busybox==vm2 md5sum(同哈希)。
- 旧 [99-103] 顺移 [108-112];三处同步(verify/壳走查表/
  ARCHITECTURE 同 commit)。

## 3. 边界(v0)

sock(unix 域)/drvfw/coredump/sysfs=后弧(记档);熵源非
密码级;inittab=壳代处理器(真 PID1 init=后弧,需 sh);busybox
重定向(>file 的 O_CREAT 面)=后弧;getdents64 每调一名(合法
POSIX 面,慢=后弧合批);realtime 无 RTC(drvfw 后弧)。

## 4. DoD

spec→实现→111 门×2→devlog→内容包 5→commit→镜像机→记忆→
直开站 41(CAPR),不停。
