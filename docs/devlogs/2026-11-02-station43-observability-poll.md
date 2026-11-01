# 站四十三:观测面(calltrace/ps blk)+ppoll——参考实现审计裁剪 H/I 兑现日

**日期**:2026-11-02
**状态**:实现完成,冒烟 6/6;门禁三枚已写入 verify([119-121],总 126)按批令不跑(43-48 统一两连)
**spec**:docs/specs/2026-11-02-station43-observability-poll.md

## 交付三面

1. **rt_calltrace**——x29 FP 链走查(rt_task.c):16 帧保险丝+内核
   窗门(窗内帧才报,用户帧止步=域分离),挂 rt_panic_fault 尾部
   =EL1 abort 面自动打栈。站 41 那种"静默腐坏三小时"的取证成本,
   以后是一屏链。demo 面=shell `calltrace` 命令(经 RT_U_CALLTRACE
   trampoline),冒烟 frames=3。
2. **ps blk 归属列**——wait_src 域(@764)最后阻塞原语归属:
   sem/mut/rx/futx/slep 各打点(rt_sync.c 两处/rt_task.c 两处/
   rt_sig.c futex 一处)。显示面 wsrc_name() 七档枚举。**定谳**:
   站 30 msg 等待经 ch->msgs 信号量阻塞→显示 "sem"(机制复用的
   如实面,非缺档);futex pend_q 型阻塞不经 sem=漏标显陈旧值,
   本站观测面自己的洞自己补(rt_sig.c 挂点)。
3. **ppoll**(号面 73,musl poll 走 ppoll)+**RT_FDOP_POLL** wire op:
   per-fd 串行往返骑站 31 fd 三元组,零新等待逻辑(超时=tick 轮询环,
   timeout 0=立即返回,ts NULL=无限)。Linux pollfd 8B 契约
   {int fd; short events; short revents}——**events@+4/revents@+6**。
   三服务器各答 POLL 脸:tty=rx_pending/ramfs=位置推进/pipe=
   count!=0(读端),POLLOUT 恒真 v0 面。polldemo 三脸:空管超时
   /写后 POLLIN/读后排空。

## 战雷三枚(判据面指向)

- **events@+4**:首版 `(u16)(ent>>16)` 把 fd 高半当 events——fd 是
  4 字节 int,events 在 +4。ev 恒 0→POLLIN 永不成立,循环吃满窗。
  服务器侧 PPDBG 取证:pipesrv 全程答 1(count=9),客户端死不认。
  判据面指向的经典再演(站 30 rcvid 反谓词同款):**被调方如实,
  调用方失聪,先看调用方的解析**。
- **pipesrv 无 POLL case**:默认 -22 落 POLLNVAL(needle 抓出);
  修=加 RT_FDOP_POLL case。
- **冒烟驱动晚挂=RX 死窗**:vuart 陷阱 console 在 TCG 下慢,
  smoke 驱动若 vmrun 后才慢慢连 console,晚挂=100% 死 RX 窗。
  verify [19] 的 `wait=on` chardev 形状才是正解(QEMU 等首连接才
  走,结构性无晚挂)。两小时法医的教训:$50 的探针贵不过 $5 的
  判据——**探针异常先跑标准判据(123/123 全绿)再谈机制**。

## 纪律

- 冒烟针线一律从真实输出拷,不凭记忆写("blk sem" 本次差点
  凭记忆落空——msg 等待显示 sem 而非 msg,逐行核 k_ps 输出格式
  后才定针)。
- 批令(43-48):站内不跑 verify 两连,门禁写而不跑;站 48 完成后
  统一两连。

## 门禁(写而不跑,总 126)

- [119] calltrace:calltrace: + frames=
- [120] polldemo:超时/POLLIN/排空三脸+done
- [121] ps blk:blk sem(服务器经信号量阻塞=确定性针线)
- 旧 [119-123] 顺移 [122-126]
