<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 第十八站内容包:四块合成站(闪断根治/PI/per-task FP/双核真并发)

口径(README 索引):
- **用户令"下一站4项工作全部需要完成"**:A vgic 在途拍守门 / B 互斥
  优先级继承 / C per-task FP 帧 / D vCPU>1,四块一战,门禁 58→62。
- **A 块真相考古**:站16 注释写对了方案、代码没落地,封窗机器又随
  量子退役——[53] 闪断的完整因果;本站三分支+回业主落地,×3 连绿。
- **D 块一门四雷**:x1 血肉(陷阱桩寄存器纪律)、VMPIDR(mpidr 虚拟化)、
  上线劈 token(握手先行)、重复迁移对+真竞态(python 手术纪律+状态锁)。
- **真并行硬证**:vm1 spin 独占 CPU0 的 8 秒窗内,CPU1 上的 Linux
  boot 行进了 210+ 行——单物理 CPU 下不可能。
- **诚实记录**:e1 一记 [41] 闪断(1/3),e2-e4 三连全绿;机制未定谳
  立观察哨,不刷绿。

文件:
- zhihu-gongzhonghao.md:知乎/公众号长文(主线:注释与代码背离+
  双核首夜四雷)
- xiaohongshu.md:小红书短文
- x-thread.md:X 8 推
- youtube.md:YouTube/B 站分镜
