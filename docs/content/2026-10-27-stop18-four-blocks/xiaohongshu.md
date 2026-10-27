<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 双核首夜,一门四雷 💥 我的 Hypervisor 真并行了

用户令:四项工作全部完成。四块一战,门禁 58→62:

1️⃣ 闪断根治——考古出土真相:
站16 注释写对了方案,代码没落地;
封窗机器又同站退役——窗永远敞着
修法:让代码长成注释的样子(三分支+回业主)✅ ×3 连绿

2️⃣ 优先级继承:9→6→9 完整弧
mid 饿死 low,high 一挂等,low 被 boost 反超完工
无 PI 则判据必红,不可假阳 📐

3️⃣ per-task FP:一个宏,两个切换点
偏移编译期断言钉死(契约要机器盯)
双 FP 任务并发,两个百万求和都精确 ✨

4️⃣ 双核真并发(PSCI 一发入魂):
🧨 雷1:x1 是 guest 的血肉——桩里拿来当 scratch,
   Linux 打出一屏 \x01,vm0 写 NUL 全哑
🧨 雷2:VMPIDR 虚拟化——客在 CPU1 恒见 cpu0
🧨 雷3:上线自报劈 token——握手先行
🧨 雷4:先排自伤(python 手术埋的重复迁移对)再归因竞态

硬证:vm1 spin 独占 CPU0 的 8 秒窗内,
CPU1 上 Linux 走了 210+ 行——单物理 CPU 下不可能 🏆

诚实账:e1 一记闪断(1/3),e2-e4 三连绿,立观察哨不刷绿

#从零写Hypervisor #双核 #SMP #RTOS #ARM64
