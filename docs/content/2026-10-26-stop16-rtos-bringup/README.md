<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 第十六站内容包:RTOS bring-up(参考RTOS 味任务内核 + SIMD per-VM 帧)

口径(README 索引):
- **里程碑**:eshine-os 的 VM0 从裸机 demo 升格为真任务内核——32 级
  位图抢占调度、100Hz tick、IRQ 驱动 shell、多任务并发;与 VM1 裸机
  对照组、VM2 Linux 三异构客并跑。门禁 48→55 双绿。
- **双根因(RTOS 本体)**:`rt_switch_to` 标 noreturn 让 GCC 在续跑点
  后排死代码(空指针炸/无限打印);空转语境缺 WFI 导致热自旋。
- **五组共享物理面收编**:异构双客炸出 VBAR/CPACR/SCTLR 系/ICH_LR0-
  VMCR/SIMD 全部需要 per-VM 帧,泵边前装后存。
- **陷阱量子的生灭**:为并发判据造的快轮转撞上关线+丢拍组合拳,
  Linux 闪断 25-75%;对照二分定谳,判据窗口改解掩蔽段后整组退役。
- **叙事**:十五站同源双镜像把共享物理面泄漏藏得天衣无缝;第一对
  异构客上机一天炸出全部——结构性地基补完的故事。

文件:
- zhihu-gongzhonghao.md:知乎/公众号长文(主线:幽灵输出认尸)
- xiaohongshu.md:小红书短文
- x-thread.md:X 8 推
- youtube.md:YouTube/B 站分镜
