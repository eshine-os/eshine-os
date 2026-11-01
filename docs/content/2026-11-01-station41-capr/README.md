# 站41 口径:CAPR 能力框架——能力与边界(序列终站)

**eshine-os** · 站 41(CAPR)· 2026-11-01

## 本站做了什么

- **能力位图**:rt_proc 增 u64 caps 四位(CAP_VFS/SPAWN/
  KILL/MM)。create=零能力,boot 壳=CAP_ALL,fork 按位
  继承,**无任何加能力路径**——self-inc-out 不靠执法,由
  构造保证(唯一 mutation=cap_drop 580 的 `caps &= ~mask`)。
- **fail-closed 执法点**(choke point 非全系统调用铠甲):
  fd 六门(open/create/unlink/dir/getdents64/fstatat)判
  CAP_VFS;fork/clone 判 CAP_SPAWN;kill/sigqueue 判
  CAP_KILL;mmap/shm/mq/pipe 判 CAP_MM。缺能力即拒
  (-EPERM),默认拒绝。
- **rlimit 自降单向**:max_fd 默认满额、max_child 同;自降
  后第 4 open -EMFILE、fork 满额 -EAGAIN;rlimit_self 583
  只降不升。
- **uuid 身份**:create 期站 40 混出器随机 8B,fork=新
  uuid(资源承继身份不承),零值 pid 换挡。
- 判据 [107-111]:capdemo(含"已开 dir fd 上 getdents64 同
  拒"的操作级执法面)/capdemo2/rlimitdemo/uiddemo/壳
  CAP_ALL 回归;门禁 111→116,两连 RESULT PASS。

## 战果:吞行自雷+三小时法医

插两道 CAP 检查时,一次编辑把 `rt_proc_create(NULL,&cp)`
整行吞掉——cp 成野指针,fork_as 拿它狂写,EL1 数据中止落
进静默驻车(wfi 永环),全系统死锁零故障行。三重伪象:
尸检栈读陈链、双 watchpoint 零命中、断点后零停点;最后
`git diff` 一照面定谳。**-Wall -Wextra 本就在构建里,当轮
必已打"cp 未初始化"警告——警告在,没人读。**

## 陈账清理

shell help 走查头部"93 步"=站 35-40 六站漏更,本站一并
清到 116 步。41=站序定稿的终站,28→41 十四站齐收。
