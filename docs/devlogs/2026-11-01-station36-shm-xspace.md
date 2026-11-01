# 站三十六 devlog:shm+xspace——共享段机器与单拷窗

日期:2026-11-01 · 作者:Julian Chu + Claude Code
状态:**已收口**——96 门(新 [90] shmdemo/[91] msgseg,旧
[90-94] 顺移 [92-96])两连 RESULT PASS 收口(r_live+r2 相邻两绿;
序列首验 r1 红在连绿前,见 §4 闪烁档)

## 0. 本站做了什么

站 29 的 fork=eager 全拷=物理隔离即存在意义;本站补上受控的
**共享面**:内核登记共享段(页自页池,create 取页清零/destroy
还页一口)+xspace 固定窗(块 8 @0x41000000/2MB,段 i 固定映
XSPACE_BASE+(i-1)*SPAN=**同段在任何进程同 VA**=参考实现 "全局
唯一共享窗"的站内形状)+VMA 第三型(pool=2=SHM,shm_id 驻
rsv 槽转正)+fork 律(**SHM VMA 子表同物理直挂零拷贝**,其余
VMA 维持 eager 全拷——共享与隔离两立分型)。

## 1. 机制落点

- **rt_shm.c**(新):段注册表(8 段×≤16 页)+四面
  (create/attach/detach/destroy,号面 560-563)+两钩
  (fork 继承=隐式 attach refs++;退场逐 SHM VMA 卸引用)。
- **页归属铁律**:段页归段所有,唯一还池入口=destroy;fork/
  munmap/退场三条页回收路径皆不触段页(munmap 拒 SHM VMA
  路由 detach;退场页回收只走块 1/6;块 8 的 L2 槽由
  as_l2_free 归池,陈 PTE 随再分配清零)。
- **VM_SET 单拷窗**(站 30 差距⑤⑥的 v0 兑现):1KB 载荷走
  共享窗=一拷(发送者写),8B 脉冲只带段引用(消息线 64B
  wire 不动),接收者按引用 attach 后原地零拷读。参考实现真身
  "免页表遍历的单拷+延迟访问"在我们机器里天然成立:页池
  恒驻+同物理直挂,不涉 walk。长消息 1024B wire+READV/
  WRITEV 镜像=站 37。
- **容量账**:L2 槽 16→24(8 proc×user/mmap/xspace),bss
  +32KB;16MB 授予内 9 块(xspace 用第 9 块 2MB,余量 1.5MB
  记边界:无 VALLOC 子窗分配器,按段整窗)。

## 2. 冒烟自雷两枚

- **msgseg 拆雷竞态**(首轮必挂):父在子退场完成前
  destroy→refs 仍 1→-EBUSY。子 exit 走 fd-close 消息往返
  (慢),父验证路快——**wait4 前置到 detach/destroy 前**
  修定。教训:refs 类生命周期对象,销毁前先等持有者死透。
- **msg_recv 参序**:sys6=nr+6 参,5 参面须补 0 填位(编译
  器即抓);as_tlb_flush 前置声明(shm_clear 先用)。

## 3. 三处同步的原子性红利

verify 顺移脚本里,全局分母替换把锚串自己也改了
([89/94]→[89/96]),**断言在写盘前抓住=嵌合体零发生**
(assert-then-write 纪律的机器兑现;对照站 27/31 两起嵌合体
是 sed 静默失败产的)。门 94→96,help 走查/ARCHITECTURE
同批。

## 4. 闪烁档(诚实记账,站 37 观察项)

两个未定谳失速脸,各现形一次:
- **空格洪流脸**:pt 的 printf 印出"counter="后 8.4M 连续
  0x20 洪流(smoke36 首轮);
- **44B 分片停摆脸**:verify r1 [88] hello 印到"pid="
  (恰=fdwrite 内联 44B 分片边界)后静默超时。
两者共族嫌疑:writev→fdwrite 44B 分片环的第二片失速=
msg_send 阻塞无回执=tty 服务器状态异常。**消融无结论**:
三钩(pfault 分支/fork 钩/exit 钩)对非 shm 流只读=语义
恒等,短路后 3 跑无失速但样本不足;冷构建探针 3/3 净;
wait4 修复后 12 连跑+两连绿中未复发(总率 ~8%)。**活捕
器具已备**:失速永不自愈=大 gdb 附件窗(pt36 同款,附加时
读各任务状态/pend 队/tty 服务器现场)。若站 37 复发即上,
定谳前不结案,不猜。

## 5. 门禁与收口

- [90] shmdemo:fork 共享判别器(子写同 VA 父读回;eager 拷
  会私有化子写=父读不到判负)+固定 VA+destroy refs=0;
  [91] msgseg:1KB via window, wire=8B+按引用 attach+destroy。
- 96 门两连绿;r1 红档+两脸闪烁记 §4。
- devlog+内容包 5 同批;commit→镜像机→记忆;直开站 37
  (posix_queue/mqueue/pipe),不停。
