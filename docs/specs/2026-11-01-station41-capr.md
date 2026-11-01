# 站 41 spec:CAPR 安全框架——能力与边界(终站)

日期:2026-11-01 · 作者:Julian Chu + Claude Code
参考实现机制白描(零拷贝红线):CAPR=能力式安全框架——句柄式
cap 表+**fail-closed**(无能力即拒,默认拒绝)+self-inc-out
(自削单向,只减不增)+rlimit 资源上限+uuid 进程身份。偏差
#24-30 已记档:我们 v0 = 站内最小形,粗粒度能力位。

## 1. 我们世界里的四件事

现状盘点:进程=站 29 proc(全权,无任何拒径);fd/IPC/内存
面全开放;无身份面(仅 pid)。

### A. cap 表(句柄式,粗粒度位图 v0)
- rt_proc 增 `u64 caps`;位定义:CAP_VFS(一切 fd 面:
  open/create/unlink/dir/stat/getdents)/CAP_SPAWN(fork/
  clone)/CAP_KILL(kill/sigqueue)/CAP_MM(mmap/shm/mq/pipe)。
- **create=0(零能力)**;boot 壳=CAP_ALL(4 位全);fork=
  按位继承(exec 同进程 v0 不变);**无任何加能力路径**=
  self-inc-out 由构造保证(只减不增是唯一方向)。
- 号面(扩展段):cap_drop 580(自削掩码,单向)/cap_get
  581(自查)/uuid_get 582/rlimit_self 583(自降上限,同
  单向)。

### B. fail-closed 执法点(默认拒绝)
- fd 层六门:rt_fd_open/create/unlink/dir/getdents64/
  fstatat 入口判 CAP_VFS,缺=−EPERM(−1)。
- sys_fork/clone 判 CAP_SPAWN;kill/sigqueue 判 CAP_KILL;
  mmap/shm_create/mq_open/pipe 判 CAP_MM。
- **执法点=choke point(六门+四系统调用),非全系统调用
  铠甲**——v0 边界:read/write(已开的 fd=已授权窗)、
  时间/随机/日志面不设卡(v0 记档)。

### C. rlimit(资源上限,自降单向)
- rt_proc 增 `u32 max_fd`(默认 RT_FD_MAX)/`u32 max_child`
  (默认 RT_PROC_MAX-1);fork 继承。
- 执法:fd 槽扫描处用满→−EMFILE(−24);fork 计子达限→
  −EAGAIN(−11)。
- rlimit_self 583 只降不升。

### D. uuid(进程身份)
- rt_proc 增 `u64 uuid`=创建时站 40 混出器随机(零=特殊,
  用 pid 换挡);fork=新 uuid(新身份,资源承继不承身份)。
- 判据:两进程 uuid 互异且非零;壳命令打印自证。

## 2. 判据(111→116)

- [107] capdemo:子自削 CAP_VFS→fdopen -EPERM;再试
  getdents64 同拒;**无加能力路径**(结构即单向)。
- [108] capdemo2:子自削 CAP_SPAWN→fork -EPERM(句柄式
  表活证);对照:未削 fork 照常([78] forkdemo 面即对照)。
- [109] rlimitdemo:子自降 max_fd=3→第 4 open −24 EMFILE;
  对照壳(默认上限)fd 照常。
- [110] uiddemo:两子 uuid 互异非零+壳自查打印。
- [111] fail-closed 回归:壳=CAP_ALL 全脸照常(整个电池=
  隐式回归;本门显式 cap_get=0xF 打印)。
- 旧 [107-111] 顺移 [112-116];三处同步同 commit。

## 3. 边界(v0)

粗粒度位图(非句柄式对象表,记档);读写不分档(已开窗即
可读写);时间/随机/日志面不设卡;无 cap 转让(仅自削);
rlimit 两项(fd/子数)非全集;uuid 非密码随机(站 40 混出器
同源,非密码级记档同律);**真实世界的 MAC/RBAC/ LSM 形=后
弧**;本站=参考实现五面(表/fail-closed/self-inc-out/rlimit/
uuid)的最小诚实站内形。

## 4. DoD

spec→实现→116 门×2(两连 RESULT PASS)→devlog→内容包 5→
commit→镜像机→记忆→**站序全图收口(41=既定序列终站)**。
