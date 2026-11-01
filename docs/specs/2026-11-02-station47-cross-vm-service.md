# 站四十七:跨 VM 服务面(safeshm 窗+vm0 设备后端)

**日期**:2026-11-02 | **序**:批令 43-48 第六站 | **基准**:参考实现
xspace 全局窗/VM_SET 白描(站27 审计:VM_SET=单拷)——站内取
**零拷共享窗**形(优记档:单拷→零拷,窗口直写)。

## §1 机制(白描落形)

- **安全窗**:宿主自有的 2MB 物理雕刻(PA 0x52000000,三客
  RAM 之外的空档)经 stage-2 双映入 vm0/vm1(IPA 0x41200000,
  块 9,2MB 粒度=stage2_build 既有形状零新机器);**配置权
  唯一属宿主**(客不可重映射/不可越窗=安全面所在)。
- **vm0 侧=设备后端形**:窗口在 RTOS stage-1 入内核 EL1-only
  (块 9,UXN+PXN);EL0 永不直访,用户面经 demo op(rt_xvm.c,
  内核代写代读)=设备驱动同律。vm1 侧裸机(MMU-off,VA=IPA
  直访窗口)。
- **协议(safeshm)**:4KB 首页 {seq,ack,text[64]};写方先写
  text 后 dmb 再 bump seq(序的诚实),读方见 seq 变→dmb→
  读 text→ack=seq(代际协议防撕裂复用;复用前写方必等 ack)。
- 零新 HVC(窗口+轮询=最小面;doorbell vIRQ=后弧记档)。

## §2 判据(xvm/xvmdemo 双端)

- vm1 客命令 `xvm`:打点 "waiting for vm0" 后轮询 seq
  (vtimer 10s 超时面);见变打印 "xvm: got '<text>'"+ack。
- vm0 壳 `xvmdemo`(demo op,内核语境):写 "ping from vm0"+
  seq++,打点 sent seq;轮询 ack(≤300 tick);vm1 acked
  即 "xvm: vm1 acked seq=N ok"。
- **门 [127]**:vmconsole 1→xvm→ctrl-^0 回 vm0→xvmdemo→
  三针线(sent/acked/got)——热键切换链上的真跨 VM 交付。
  vm1 输出走 vuart 陷阱 [vm1] 前缀(前台归属不碍后台出声)。

## §3 边界与记档

- vm2(Linux)不入窗 v0(Linux 侧驱动=后弧);doorbell vIRQ
  后弧;窗口 text 64B(长消息面沿站 30 长槽思路=后弧)。
- PA 0x52000000 空档定谳:vm0 RAM 顶 0x51000000/vm1 顶
  0x52000000/vm2 基 0x58000000,窗居 vm1 顶与 vm2 基之间。
