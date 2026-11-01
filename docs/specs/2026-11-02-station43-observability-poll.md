# 站 43 spec:观测面+poll(审计收尾站 1/6,批次 43-48)

日期:2026-11-02 · 作者:Julian Chu + Claude Code
上游:docs/specs/2026-11-01-station41-post-reference-audit.md §1 H/I
门禁:批次制(43-48 开发完统一两连;本站新门先写后验)

## 1. 面

### A. EL1 abort 终态面补打印(本站第一优先=还债)
- 站 41 法医弧根因:EL1 数据中止落**静默驻车**(原 guest_park
  语义),零故障行,三小时才靠 diff 定谳。EL0 终态面早已打印
  (esr/elr/far 三行+panic 指名);**EL1 面补齐同款**——未知 EC/
  内核态中止先打 esr/elr/far/当前任务名再驻车。机器背书:
  下一次内核雷从"哑死"变"开口",省一次完整验尸。

### B. calltrace(FP 链回溯)
- 内核态 FP(x29)链逐帧走:打印 pc+符号名,帧数上限 16+环保险丝
  (野帧跳出即收);挂点:rt_panic 体内自动触发(崩溃即回溯);
  shell 命令 calltrace 手动活证(当前任务直走)。
- -O2 帧指针保真核验:实测打印帧链与调用函数序列吻合。

### C. ps 观测扩展(阻塞对象归属列)
- rt_task 增 wait_src(阻塞在哪个对象:sem/mutex/queue/msg/chid/
  futex 等,由各 take/pend 路径顺手登记一 u32 标签)。
- ps 输出加一列;demo:pi/mutex 持锁窗内 ps 截图=等待者行显示
  归属(参考实现 GET_TASK_BLOCK_INFO 白描的站内最小形)。

### D. poll(ppoll 73 真号)
- v0 事件面:POLLIN(可读)/POLLOUT(可写)/POLLERR 三族;
- 可读判据=resmgr 服务器侧每 ocb 的可读状态查询消息(roundtrip
  不做,先走**本地面**:管道/ramfs 的 ocb off<size 即可读,fifo
  后弧;EL0 shell 的 demo=管道已写未读=POLLIN,空=POLLIN 无)。
- musl/busybox 的 ppoll 参数契约(fd 数组+n+timeout 从 musl 头)。

## 2. 判据(写门不跑,批次末验)

- [124] abortdemo?否——EL1 abort 打印=判据藏在单元测(制造一次
  内核野访,验打印三行)。**插一个可观测的自杀命令**风险大;
  改为:panic 面的 calltrace 活证(command)+EL1 打印由
  静态走查记档。门:[124] calltrace 命令(帧数≥4+符号名打印)。
- [124] calltrace:手动命令直走当前任务帧链,验帧数+名。
- [125] psdemo:pi/mutex 窗内 ps 截=归属列活证。
- [126] polldemo:管道三脸(未读 POLLIN/读后无/超时返 0)。
- (门号在批次末按实际全表顺移定稿)

## 3. 边界(v0)

ppoll 仅本地可读态(roundtrip 可读查询=后弧);poll 不含
epoll;calltrace 仅 EL1 帧(用户帧回溯=后弧);wait_src 登记点
仅高频原语(sem/mutex/futex/msg);timeout 语义=粗粒度 tick。

## 4. DoD(批次制)

spec→实现→冒烟电池自证→devlog/内容包→commit→镜像机→记忆;
verify 新门写进脚本+走查表+ARCHITECTURE 同 commit;
**两连=批次末(站 48 后)统一**。
