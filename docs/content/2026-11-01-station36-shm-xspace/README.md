# 站36 口径:shm+xspace——共享段机器与单拷窗

**eshine-os** · 站 36(shm+xspace)· 2026-11-01

## 本站做了什么

站 29 的 fork=eager 全拷=物理隔离即存在意义;本站补上受控
的**共享面**:

- **内核登记共享段**(rt_shm.c):8 段×≤16 页,页自页池,
  create 即取页清零/destroy 唯一还池口;refs=attach 计数,
  非零销毁拒。
- **xspace 固定窗**(块 8 @0x41000000/2MB):段 i 固定映
  同一 VA——**同段在任何进程同 VA**=参考实现"全局唯一共享窗"
  的站内形状。
- **VMA 第三型**(pool=2=SHM)+ **fork 律**:SHM VMA 子表
  **同物理直挂零拷贝**(共享与站 29 eager 隔离两立分型)。
- **页归属铁律**:段页归段所有,fork/munmap/退场三条页回收
  路径皆不触;唯一还池入口=destroy。
- **单拷窗**(站 30 挂账⑤⑥的 v0 兑现):1KB 载荷走共享窗=
  一拷,8B 脉冲只带段引用,接收者按引用 attach 后原地零拷读
  ——参考实现 VM_SET"免页表遍历的单拷+延迟访问"真身在我们
  机器里天然成立(页池恒驻+同物理直挂)。

## 判据

[90] shmdemo:fork 共享判别器(子写同 VA,父读回;eager 拷
会私有化子写=读不到判负);[91] msgseg:1024B via window,
wire=8B。

## 门禁

96 门 ×2 双绿(新 [90-91],旧 [90-94] 顺移 [92-96])。

## 诚实边界(v0)

无 POSIX 名注册(shm_open/mq=站 37+);无 VALLOC 子窗分配;
无 COW(共享=拷贝的对立面,私有页仍 eager);attach 重复拒;
destroy 非零 refs 拒;VM 间共享=后弧;段页 RW 恒。

## 关键词

ARM64 hypervisor · clean-room · RTOS · 共享内存 ·
xspace 固定窗 · 单拷消息 · fork 语义 · 页归属 · VMA
